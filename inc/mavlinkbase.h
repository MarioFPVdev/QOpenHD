#ifndef MAVLINKBASE_H
#define MAVLINKBASE_H

#include <QObject>
#include <QtQuick>


#include <openhd/mavlink.h>
#include "constants.h"

#include "util.h"


class QUdpSocket;

typedef enum MavlinkType {
    MavlinkTypeUDP,
    MavlinkTypeTCP
} MavlinkType;

typedef enum MicroserviceTarget {
    MicroserviceTargetGround,
    MicroserviceTargetAir,
    MicroserviceTargetNone
} MicroserviceTarget;

typedef enum MavlinkState {
    MavlinkStateDisconnected,
    MavlinkStateConnected,
    MavlinkStateGetParameters,
    MavlinkStateIdle
} MavlinkState;

typedef enum MavlinkCommandState {
    MavlinkCommandStateReady,
    MavlinkCommandStateSend,
    MavlinkCommandStateWaitACK,
    MavlinkCommandStateDone,
    MavlinkCommandStateFailed
} MavlinkCommandState;

typedef enum MavlinkCommandType {
    MavlinkCommandTypeLong,
    MavlinkCommandTypeInt
} MavlinkCommandType;

class MavlinkCommand  {
public:
    MavlinkCommand(MavlinkCommandType command_type) : m_command_type(command_type) {}
    MavlinkCommandType m_command_type;
    uint16_t command_id = 0;
    uint8_t retry_count = 0;

    uint8_t long_confirmation = 0;
    float long_param1 = 0;
    float long_param2 = 0;
    float long_param3 = 0;
    float long_param4 = 0;
    float long_param5 = 0;
    float long_param6 = 0;
    float long_param7 = 0;


    uint8_t int_frame = 0;
    uint8_t int_current = 0;
    uint8_t int_autocontinue = 0;
    float int_param1 = 0;
    float int_param2 = 0;
    float int_param3 = 0;
    float int_param4 = 0;
    int   int_param5 = 0;
    int   int_param6 = 0;
    float int_param7 = 0;
};

#if defined(ENABLE_RC)
class MavlinkRC  {
public:
    uint ch1 = 0;
    uint ch2 = 0;
    uint ch3 = 0;
    uint ch4 = 0;
    uint ch5 = 0;
    uint ch6 = 0;
    uint ch7 = 0;
    uint ch8 = 0;
    uint ch9 = 0;
    uint ch10 = 0;
    uint ch11 = 0;
    uint ch12 = 0;
    uint ch13 = 0;
    uint ch14 = 0;
    uint ch15 = 0;
    uint ch16 = 0;
    uint ch17 = 0;
    uint ch18 = 0;

};
#endif


class MavlinkBase: public QObject {
    Q_OBJECT

public:
    explicit MavlinkBase(QObject *parent = nullptr, MavlinkType mavlink_type = MavlinkTypeUDP);


    Q_INVOKABLE QVariantMap getAllParameters();

    Q_PROPERTY(bool loading MEMBER m_loading WRITE set_loading NOTIFY loadingChanged)
    void set_loading(bool loading);

    Q_PROPERTY(bool saving MEMBER m_saving WRITE set_saving NOTIFY savingChanged)
    void set_saving(bool saving);

    Q_INVOKABLE void fetchParameters();

    void sendHeartbeat();
    void sendRC();

    Q_INVOKABLE void get_Mission_Items(int count);
    Q_INVOKABLE void send_Mission_Ack();


    Q_PROPERTY(qint64 last_heartbeat MEMBER m_last_heartbeat WRITE set_last_heartbeat NOTIFY last_heartbeat_changed)
    void set_last_heartbeat(qint64 last_heartbeat);

    Q_PROPERTY(qint64 last_attitude MEMBER m_last_attitude WRITE set_last_attitude NOTIFY last_attitude_changed)
    void set_last_attitude(qint64 last_attitude);

    Q_PROPERTY(qint64 last_battery MEMBER m_last_battery WRITE set_last_battery NOTIFY last_battery_changed)
    void set_last_battery(qint64 last_battery);

    Q_PROPERTY(qint64 last_gps MEMBER m_last_gps WRITE set_last_gps NOTIFY last_gps_changed)
    void set_last_gps(qint64 last_gps);

    Q_PROPERTY(qint64 last_vfr MEMBER m_last_vfr WRITE set_last_vfr NOTIFY last_vfr_changed)
    void set_last_vfr(qint64 last_vfr);


    Q_INVOKABLE void setGroundIP(QString address);   

signals:
    void last_heartbeat_changed(qint64 last_heartbeat);
    void last_attitude_changed(qint64 last_attitude);
    void last_battery_changed(qint64 last_battery);
    void last_gps_changed(qint64 last_gps);
    void last_vfr_changed(qint64 last_vfr);
    void setup();
    void processMavlinkMessage(mavlink_message_t msg);

    void allParametersChanged();

    void loadingChanged(bool loading);
    void savingChanged(bool saving);

    void commandDone();
    void commandFailed();

    void bindError();

public slots:
    void onStarted();    
    void request_Mission_Changed();
#if defined(ENABLE_RC)
    void receive_RC_Update(uint rc1,
                           uint rc2,
                           uint rc3,
                           uint rc4,
                           uint rc5,
                           uint rc6,
                           uint rc7,
                           uint rc8,
                           uint rc9,
                           uint rc10,
                           uint rc11,
                           uint rc12,
                           uint rc13,
                           uint rc14,
                           uint rc15,
                           uint rc16,
                           uint rc17,
                           uint rc18
                           );
    void joystick_Present_Changed(bool joystickPresent);
#endif
protected slots:
    void processMavlinkUDPDatagrams();
    void processMavlinkTCPData();

    void onTCPDisconnected();
    void onTCPConnected();    

protected:
    void stateLoop();
    void commandStateLoop();
    bool isConnectionLost();
    void resetParamVars();
    void processData(QByteArray data);
    void sendData(char* data, int len);
    void sendCommand(MavlinkCommand command);   
    void setDataStreamRate(MAV_DATA_STREAM streamType, uint8_t hz);
    void requestAutopilotInfo();

    void reconnectTCP();

    QVariantMap m_allParameters;

    MavlinkState state = MavlinkStateDisconnected;
    MavlinkCommandState m_command_state = MavlinkCommandStateReady;

    uint16_t parameterCount = 0;
    uint16_t parameterIndex = 0;

    qint64 parameterLastReceivedTime;

    qint64 initialConnectTimer;

    bool m_loading = false;
    bool m_saving = false;

    bool m_restrict_sysid = true;
    bool m_restrict_compid = true;

protected:
    OpenHDUtil m_util;
    quint8 targetSysID;
    quint8 targetCompID;

    quint16 localPort = 14550;

    QString groundAddress;
    quint16 groundUDPPort = 14550;
    quint16 groundTCPPort = 5761;

    std::atomic<bool> m_ground_available;
    MavlinkType m_mavlink_type;
    QAbstractSocket *mavlinkSocket = nullptr;

    mavlink_status_t r_mavlink_status;

    qint64 m_last_heartbeat = -1;
    qint64 m_last_attitude = -1;
    qint64 m_last_battery = -1;
    qint64 m_last_gps = -1;
    qint64 m_last_vfr = -1;

    qint64 last_heartbeat_timestamp = 0;
    qint64 last_battery_timestamp = 0;
    qint64 last_gps_timestamp = 0;
    qint64 last_vfr_timestamp = 0;
    qint64 last_attitude_timestamp = 0;

    QTimer* timer = nullptr;
    QTimer* m_heartbeat_timer = nullptr;

    QTimer* m_rc_timer = nullptr;

    QTimer* m_command_timer = nullptr;
    QTimer* tcpReconnectTimer = nullptr;

    uint64_t m_last_boot = 0;

    uint64_t m_command_sent_timestamp = 0;

    std::shared_ptr<MavlinkCommand> m_current_command;

    uint m_rc1 = 0;
    uint m_rc2 = 0;
    uint m_rc3 = 0;
    uint m_rc4 = 0;
    uint m_rc5 = 0;
    uint m_rc6 = 0;
    uint m_rc7 = 0;
    uint m_rc8 = 0;
    uint m_rc9 = 0;
    uint m_rc10 = 0;
    uint m_rc11 = 0;
    uint m_rc12 = 0;
    uint m_rc13 = 0;
    uint m_rc14 = 0;
    uint m_rc15 = 0;
    uint m_rc16 = 0;
    uint m_rc17 = 0;
    uint m_rc18 = 0;


};

#endif
