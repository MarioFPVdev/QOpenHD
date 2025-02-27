#include "mavlinkbase.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#ifndef __windows__
#include <unistd.h>
#endif

#include <QtNetwork>
#include <QThread>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QFuture>

#include <openhd/mavlink.h>
#include "openhdrc.h"

#include "util.h"
#include "constants.h"

/*
 * Note: this class now has several crude hacks for handling the different sysid/compid combinations
 * used by different flight controller firmware, this is the wrong way to do it but won't cause any
 * problems yet.
 *
 */

MavlinkBase::MavlinkBase(QObject *parent,  MavlinkType mavlink_type): QObject(parent), m_ground_available(false), m_mavlink_type(mavlink_type) {
    qDebug() << "MavlinkBase::MavlinkBase()";
}

void MavlinkBase::onStarted() {
    auto type = m_mavlink_type == MavlinkTypeTCP ? "TCP" : "UDP";
    qDebug() << "MavlinkBase::onStarted(" << type << ")";

    switch (m_mavlink_type) {
        case MavlinkTypeUDP: {
            mavlinkSocket = new QUdpSocket(this);
            auto bindStatus = mavlinkSocket->bind(QHostAddress::Any, localPort);
            if (!bindStatus) {
                emit bindError();
            }
            connect(mavlinkSocket, &QUdpSocket::readyRead, this, &MavlinkBase::processMavlinkUDPDatagrams);
            break;
        }
        case MavlinkTypeTCP: {
            mavlinkSocket = new QTcpSocket(this);
            connect(mavlinkSocket, &QTcpSocket::readyRead, this, &MavlinkBase::processMavlinkTCPData);
            connect(mavlinkSocket, &QTcpSocket::connected, this, &MavlinkBase::onTCPConnected);
            connect(mavlinkSocket, &QTcpSocket::disconnected, this, &MavlinkBase::onTCPDisconnected);
	        ((QTcpSocket*)mavlinkSocket)->connectToHost(groundAddress, groundTCPPort);
            tcpReconnectTimer = new QTimer(this);
            connect(tcpReconnectTimer, &QTimer::timeout, this, &MavlinkBase::reconnectTCP);
            tcpReconnectTimer->start(1000);
            break;
        }
    }

    m_command_timer = new QTimer(this);
    connect(m_command_timer, &QTimer::timeout, this, &MavlinkBase::commandStateLoop);
    m_command_timer->start(200);


    m_heartbeat_timer = new QTimer(this);
    connect(m_heartbeat_timer, &QTimer::timeout, this, &MavlinkBase::sendHeartbeat);
    m_heartbeat_timer->start(5000);

    #if defined(ENABLE_RC)
    m_rc_timer = new QTimer(this);        
    connect(m_rc_timer, &QTimer::timeout, this, &MavlinkBase::sendRC);
    #endif


    emit setup();
}

void MavlinkBase::onTCPConnected() {
    qDebug() << "MavlinkBase::onTCPConnected()";
}

void MavlinkBase::onTCPDisconnected() {
    reconnectTCP();
}

void MavlinkBase::reconnectTCP() {
    if (groundAddress.isEmpty()) {
        return;
    }

    if (((QTcpSocket*)mavlinkSocket)->state() == QAbstractSocket::UnconnectedState) {
        ((QTcpSocket*)mavlinkSocket)->connectToHost(groundAddress, groundTCPPort);
    }
}

void MavlinkBase::setGroundIP(QString address) {
    if (!mavlinkSocket) {
        return;
    }
    bool reconnect = false;
    if (groundAddress != address) {
        reconnect = true;
    }

    groundAddress = address;

    if (reconnect) {
        switch (m_mavlink_type) {
            case MavlinkTypeTCP: {
                if (((QTcpSocket*)mavlinkSocket)->state() == QAbstractSocket::ConnectedState) {
                    ((QTcpSocket*)mavlinkSocket)->disconnectFromHost();
                }
                break;
            }
            default: {
                break;
            }
        }
    }
}


void MavlinkBase::set_loading(bool loading) {
    m_loading = loading;
    emit loadingChanged(m_loading);
}


void MavlinkBase::set_saving(bool saving) {
    m_saving = saving;
    emit savingChanged(m_saving);
}


void MavlinkBase::sendData(char* data, int len) {
    switch (m_mavlink_type) {
        case MavlinkTypeUDP: {
            ((QUdpSocket*)mavlinkSocket)->writeDatagram((char*)data, len, QHostAddress(groundAddress), groundUDPPort);
            break;
        }
        case MavlinkTypeTCP: {
            if (((QTcpSocket*)mavlinkSocket)->state() == QAbstractSocket::ConnectedState) {
                ((QTcpSocket*)mavlinkSocket)->write((char*)data, len);
            }
            break;
        }
    }
}

QVariantMap MavlinkBase::getAllParameters() {
    qDebug() << "MavlinkBase::getAllParameters()";
    return m_allParameters;
}


void MavlinkBase::fetchParameters() {
    QSettings settings;
    int mavlink_sysid = settings.value("mavlink_sysid", m_util.default_mavlink_sysid()).toInt();

    mavlink_message_t msg;
    mavlink_msg_param_request_list_pack(mavlink_sysid, MAV_COMP_ID_MISSIONPLANNER, &msg, targetSysID, targetCompID);

    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    int len = mavlink_msg_to_send_buffer(buffer, &msg);

    sendData((char*)buffer, len);
}


void MavlinkBase::sendHeartbeat() {
    QSettings settings;
    int mavlink_sysid = settings.value("mavlink_sysid", m_util.default_mavlink_sysid()).toInt();

    mavlink_message_t msg;

    mavlink_msg_heartbeat_pack(mavlink_sysid, MAV_COMP_ID_MISSIONPLANNER, &msg, MAV_TYPE_GCS, MAV_AUTOPILOT_INVALID, 0, 0, 0);

    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    int len = mavlink_msg_to_send_buffer(buffer, &msg);

    sendData((char*)buffer, len);
}

#if defined(ENABLE_RC)
void MavlinkBase::joystick_Present_Changed(bool joystickPresent) {
    qDebug() << "MavlinkBase::joystick_Present_Changed:"<< joystickPresent;
    if (joystickPresent == true){
        qDebug() << "MavlinkBase::joystick_Present_Changed: starting timer for RC msgs";
        m_rc_timer->start(20);
    }
    else{
        qDebug() << "MavlinkBase::joystick_Present_Changed: stopping timer for RC msgs";
        m_rc_timer->stop();
    }

}

void MavlinkBase::receive_RC_Update(uint rc1,uint rc2,uint rc3,uint rc4,uint rc5,uint rc6,uint rc7,uint rc8,
                                    uint rc9,uint rc10,uint rc11,uint rc12,uint rc13,uint rc14,uint rc15,uint rc16,uint rc17,uint rc18) {

    qDebug() << "MavlinkBase::receive_RC_Update="<< rc1;
    m_rc1 = rc1;
    m_rc2 = rc2;
    m_rc3 = rc3;
    m_rc4 = rc4;
    m_rc5 = rc5;
    m_rc6 = rc6;
    m_rc7 = rc7;
    m_rc8 = rc8;
    m_rc9 = rc9;
    m_rc10 = rc10;
    m_rc11 = rc11;
    m_rc12 = rc12;
    m_rc13 = rc13;
    m_rc14 = rc14;
    m_rc15 = rc15;
    m_rc16 = rc16;
    m_rc17 = rc17;
    m_rc18 = rc18;

}

void MavlinkBase::sendRC () {
    QSettings settings;
    bool enable_rc = settings.value("enable_rc", m_util.default_mavlink_sysid()).toBool();

    if (enable_rc == true){
        int mavlink_sysid = settings.value("mavlink_sysid", m_util.default_mavlink_sysid()).toInt();

        mavlink_message_t msg;

        //TODO mavlink sysid is hard coded at 255... in app its default is 225
        mavlink_msg_rc_channels_override_pack(255, MAV_COMP_ID_MISSIONPLANNER, &msg, targetSysID, targetCompID, m_rc1, m_rc2, m_rc3, m_rc4, m_rc5, m_rc6, m_rc7, m_rc8, m_rc9, m_rc10, m_rc11, m_rc12, m_rc13, m_rc14, m_rc15, m_rc16, m_rc17, m_rc18);

        uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
        int len = mavlink_msg_to_send_buffer(buffer, &msg);

        sendData((char*)buffer, len);
    }
    else {
        return;
    }

}
#endif

void MavlinkBase::requestAutopilotInfo() {
    qDebug() << "MavlinkBase::request_Autopilot_Info";
    QSettings settings;
    int mavlink_sysid = settings.value("mavlink_sysid", m_util.default_mavlink_sysid()).toInt();

    mavlink_message_t msg;

    mavlink_msg_autopilot_version_request_pack(mavlink_sysid, MAV_COMP_ID_MISSIONPLANNER, &msg, targetSysID,targetCompID);

    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    int len = mavlink_msg_to_send_buffer(buffer, &msg);

    sendData((char*)buffer, len);
}


void MavlinkBase::request_Mission_Changed() {
    qDebug() << "MavlinkBase::request_Mission_Changed";

    QSettings settings;
    int mavlink_sysid = settings.value("mavlink_sysid", m_util.default_mavlink_sysid()).toInt();

    mavlink_message_t msg;

    mavlink_msg_mission_request_list_pack(mavlink_sysid, MAV_COMP_ID_MISSIONPLANNER, &msg, targetSysID,targetCompID,0);

    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    int len = mavlink_msg_to_send_buffer(buffer, &msg);

    sendData((char*)buffer, len);

}

void MavlinkBase::get_Mission_Items(int total) {
    qDebug() << "MavlinkBase::get_Mission_Items total="<< total;
    QSettings settings;
    int mavlink_sysid = settings.value("mavlink_sysid", m_util.default_mavlink_sysid()).toInt();

    mavlink_message_t msg;

    int current_seq;

    for (current_seq = 1; current_seq < total; ++current_seq){
        //qDebug() << "MavlinkBase::get_Mission_Items current="<< current_seq;

        mavlink_msg_mission_request_int_pack(mavlink_sysid, MAV_COMP_ID_MISSIONPLANNER, &msg, targetSysID,targetCompID,current_seq,0);

        uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
        int len = mavlink_msg_to_send_buffer(buffer, &msg);

        sendData((char*)buffer, len);
    }
}

void MavlinkBase::send_Mission_Ack() {
    qDebug() << "MavlinkBase::send_Mission_Ack";

    QSettings settings;
    int mavlink_sysid = settings.value("mavlink_sysid", m_util.default_mavlink_sysid()).toInt();

    mavlink_message_t msg;

    mavlink_msg_mission_ack_pack(mavlink_sysid, MAV_COMP_ID_MISSIONPLANNER, &msg, targetSysID,targetCompID,0,0);

    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    int len = mavlink_msg_to_send_buffer(buffer, &msg);

    sendData((char*)buffer, len);

}

bool MavlinkBase::isConnectionLost() {
    /* we want to know if a heartbeat has been received (not -1, the default)
       but not in the last 5 seconds.*/
    if (m_last_heartbeat > -1 && m_last_heartbeat < 5000) {
        return false;
    }
    return true;
}

void MavlinkBase::resetParamVars() {
    m_allParameters.clear();
    parameterCount = 0;
    parameterIndex = 0;
    initialConnectTimer = -1;
    /* give the MavlinkStateGetParameters state a chance to receive a parameter
       before timing out */
    parameterLastReceivedTime = QDateTime::currentMSecsSinceEpoch();
}


void MavlinkBase::stateLoop() {
    qint64 current_timestamp = QDateTime::currentMSecsSinceEpoch();
    set_last_heartbeat(current_timestamp - last_heartbeat_timestamp);

    set_last_attitude(current_timestamp - last_attitude_timestamp);
    set_last_battery(current_timestamp - last_battery_timestamp);
    set_last_gps(current_timestamp - last_gps_timestamp);
    set_last_vfr(current_timestamp - last_vfr_timestamp);

    return;

    switch (state) {
        case MavlinkStateDisconnected: {
            set_loading(false);
            set_saving(false);
            if (m_ground_available) {
                state = MavlinkStateConnected;
            }
            break;
        }
        case MavlinkStateConnected: {
            if (initialConnectTimer == -1) {
                initialConnectTimer = QDateTime::currentMSecsSinceEpoch();
            } else if (current_timestamp - initialConnectTimer < 5000) {
                state = MavlinkStateGetParameters;
                resetParamVars();
                fetchParameters();
            }
            break;
        }
        case MavlinkStateGetParameters: {
            set_loading(true);
            set_saving(false);
            qint64 currentTime = QDateTime::currentMSecsSinceEpoch();

            if (isConnectionLost()) {
                resetParamVars();
                m_ground_available = false;
                state = MavlinkStateDisconnected;
            }

            if ((parameterCount != 0) && parameterIndex == (parameterCount - 1)) {
                emit allParametersChanged();
                state = MavlinkStateIdle;
            }

            if (currentTime - parameterLastReceivedTime > 7000) {
                resetParamVars();
                m_ground_available = false;
                state = MavlinkStateDisconnected;
            }
            break;
        }
        case MavlinkStateIdle: {
            set_loading(false);

            if (isConnectionLost()) {
                resetParamVars();
                m_ground_available = false;
                state = MavlinkStateDisconnected;
            }

            break;
        }
    }
}


void MavlinkBase::processMavlinkTCPData() {
    QByteArray data = mavlinkSocket->readAll();
    processData(data);
}


void MavlinkBase::processMavlinkUDPDatagrams() {
    QByteArray datagram;

    while ( ((QUdpSocket*)mavlinkSocket)->hasPendingDatagrams()) {
        m_ground_available = true;

        datagram.resize(int(((QUdpSocket*)mavlinkSocket)->pendingDatagramSize()));
        QHostAddress _groundAddress;
        quint16 groundPort;
         ((QUdpSocket*)mavlinkSocket)->readDatagram(datagram.data(), datagram.size(), &_groundAddress, &groundPort);
        groundUDPPort = groundPort;
        processData(datagram);
    }
}


void MavlinkBase::processData(QByteArray data) {
    typedef QByteArray::Iterator Iterator;
    mavlink_message_t msg;

    for (Iterator i = data.begin(); i != data.end(); i++) {
        char c = *i;

        uint8_t res = mavlink_parse_char(MAVLINK_COMM_0, (uint8_t)c, &msg, &r_mavlink_status);

        if (res) {
            /*
             * Not the target we're talking to, so reject it
             */
            if (m_restrict_sysid && (msg.sysid != targetSysID)) {
                return;
            }

            if (m_restrict_compid && (msg.compid != targetCompID)) {
                return;
            }

            // process ack messages in the base class, subclasses will receive a signal
            // to indicate success or failure
            if (msg.msgid == MAVLINK_MSG_ID_COMMAND_ACK) {
                mavlink_command_ack_t ack;
                mavlink_msg_command_ack_decode(&msg, &ack);
                switch (ack.result) {
                    case MAV_CMD_ACK_OK: {
                        m_command_state = MavlinkCommandStateDone;
                        break;
                    }
                    default: {
                        m_command_state = MavlinkCommandStateFailed;
                        break;
                    }
                }
            } else {
                emit processMavlinkMessage(msg);
            }
        }
    }
}


void MavlinkBase::set_last_heartbeat(qint64 last_heartbeat) {
    m_last_heartbeat = last_heartbeat;
    emit last_heartbeat_changed(m_last_heartbeat);
}

void MavlinkBase::set_last_attitude(qint64 last_attitude) {
    m_last_attitude = last_attitude;
    emit last_attitude_changed(m_last_attitude);
}

void MavlinkBase::set_last_battery(qint64 last_battery) {
    m_last_battery = last_battery;
    emit last_battery_changed(m_last_battery);
}

void MavlinkBase::set_last_gps(qint64 last_gps) {
    m_last_gps = last_gps;
    emit last_gps_changed(m_last_gps);
}

void MavlinkBase::set_last_vfr(qint64 last_vfr) {
    m_last_vfr = last_vfr;
    emit last_vfr_changed(m_last_vfr);   
}

void MavlinkBase::setDataStreamRate(MAV_DATA_STREAM streamType, uint8_t hz) {

    QSettings settings;

    int mavlink_sysid = settings.value("mavlink_sysid", m_util.default_mavlink_sysid()).toInt();


    mavlink_message_t msg;
    msg.sysid = mavlink_sysid;
    msg.compid = MAV_COMP_ID_MISSIONPLANNER;

    /*
     * This only sends the message to sysid 1 compid 1 because nothing else responds to this
     * message anyway, iNav uses a fixed rate and so does betaflight
     *
     */
    mavlink_msg_request_data_stream_pack(mavlink_sysid, MAV_COMP_ID_MISSIONPLANNER, &msg, 1, MAV_COMP_ID_AUTOPILOT1, streamType, hz, 1);

    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    int len = mavlink_msg_to_send_buffer(buffer, &msg);

    sendData((char*)buffer, len);
}



/*
 * This is the entry point for sending mavlink commands to any component, including flight
 * controllers and microservices.
 *
 * We accept a MavlinkCommand subclass with the fields set according to the type of command
 * being sent, and then we switch the state machine running in commandStateLoop() to the
 * sending state.
 *
 * The state machine then takes care of waiting for a command acknowledgement, and if one
 * is not received within the timeout, the command is resent up to 5 times.
 *
 * Subclasses are responsible for connecting a slot to the commandDone and commandFailed
 * signals to further handle the result.
 *
 */
void MavlinkBase::sendCommand(MavlinkCommand command) {
    m_current_command.reset(new MavlinkCommand(command));
    m_command_state = MavlinkCommandStateSend;
}


void MavlinkBase::commandStateLoop() {
    switch (m_command_state) {
        case MavlinkCommandStateReady: {
            // do nothing, no command being sent
            break;
        }
        case MavlinkCommandStateSend: {
        qDebug() << "CMD SEND";
            mavlink_message_t msg;
            m_command_sent_timestamp = QDateTime::currentMSecsSinceEpoch();

            QSettings settings;

            int mavlink_sysid = settings.value("mavlink_sysid", m_util.default_mavlink_sysid()).toInt();

            //qDebug() << "SYSID=" << mavlink_sysid;
            //qDebug() << "Target SYSID=" << targetSysID;

            if (m_current_command->m_command_type == MavlinkCommandTypeLong) {
                mavlink_msg_command_long_pack(mavlink_sysid, MAV_COMP_ID_MISSIONPLANNER, &msg, targetSysID, targetCompID, m_current_command->command_id, m_current_command->long_confirmation, m_current_command->long_param1, m_current_command->long_param2, m_current_command->long_param3, m_current_command->long_param4, m_current_command->long_param5, m_current_command->long_param6, m_current_command->long_param7);
            } else {
                mavlink_msg_command_int_pack(mavlink_sysid, MAV_COMP_ID_MISSIONPLANNER, &msg, targetSysID, targetCompID, m_current_command->int_frame, m_current_command->command_id, m_current_command->int_current, m_current_command->int_autocontinue, m_current_command->int_param1, m_current_command->int_param2, m_current_command->int_param3, m_current_command->int_param4, m_current_command->int_param5, m_current_command->int_param6, m_current_command->int_param7);          
            }
            uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
            int len = mavlink_msg_to_send_buffer(buffer, &msg);

            sendData((char*)buffer, len);

            // now wait for ack
            m_command_state = MavlinkCommandStateWaitACK;

            break;
        }
        case MavlinkCommandStateWaitACK: {
        qDebug() << "CMD ACK";
            qint64 current_timestamp = QDateTime::currentMSecsSinceEpoch();
            auto elapsed = current_timestamp - m_command_sent_timestamp;

            if (elapsed > 200) {
                // no ack in 200ms, cancel or resend
                qDebug() << "CMD RETRY";
                if (m_current_command->retry_count >= 5) {
                    m_command_state = MavlinkCommandStateFailed;
                    m_current_command.reset();
                    return;
                }
                m_current_command->retry_count = m_current_command->retry_count + 1;
                if (m_current_command->m_command_type == MavlinkCommandTypeLong) {
                    /* incremement the confirmation parameter according to the Mavlink command
                       documentation */
                    m_current_command->long_confirmation = m_current_command->long_confirmation + 1;
                }
                m_command_state = MavlinkCommandStateSend;
            }
            break;
        }
        case MavlinkCommandStateDone: {
            qDebug() << "CMD DONE";
            m_current_command.reset();
            emit commandDone();
            m_command_state = MavlinkCommandStateReady;
            break;
        }
        case MavlinkCommandStateFailed: {
            qDebug() << "CMD FAIL";
            m_current_command.reset();
            emit commandFailed();
            m_command_state = MavlinkCommandStateReady;
            break;
        }
    }
}
