/*
 / _____)             _              | |
( (____  _____ ____ _| |_ _____  ____| |__
 \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 _____) ) ____| | | || |_| ____( (___| | | |
(______/|_____)_|_|_| \__)_____)\____)_| |_|
    (C)2013 Semtech
 ___ _____ _   ___ _  _____ ___  ___  ___ ___
/ __|_   _/_\ / __| |/ / __/ _ \| _ \/ __| __|
\__ \ | |/ _ \ (__| ' <| _| (_) |   / (__| _|
|___/ |_/_/ \_\___|_|\_\_| \___/|_|_\\___|___|
embedded.connectivity.solutions===============

Description: LoRa MAC layer implementation

License: Revised BSD License, see LICENSE.TXT file include in the project

Maintainer: Miguel Luis ( Semtech ), Gregory Cristian ( Semtech ) and Daniel Jaeckle ( STACKFORCE )
*/
#include "board.h"

#include "LoRaMac.h"
#include "region/Region.h"
#include "LoRaMacCrypto.h"
#include "LoRaMacTest.h"



/*!
 * Maximum PHY layer payload size
 */
#define LORAMAC_PHY_MAXPAYLOAD                      255

/*!
 * Maximum MAC commands buffer size
 */
#define LORA_MAC_COMMAND_MAX_LENGTH                 15

/*!
 * LoRaMac region.
 */
static LoRaMacRegion_t LoRaMacRegion;

/*!
 * LoRaMac duty cycle for the back-off procedure during the first hour.
 */
#define BACKOFF_DC_1_HOUR                           100

/*!
 * LoRaMac duty cycle for the back-off procedure during the next 10 hours.
 */
#define BACKOFF_DC_10_HOURS                         1000

/*!
 * LoRaMac duty cycle for the back-off procedure during the next 24 hours.
 */
#define BACKOFF_DC_24_HOURS                         10000

/*!
 * Device IEEE EUI
 */
static uint8_t *LoRaMacDevEui;

/*!
 * Application IEEE EUI
 */
static uint8_t *LoRaMacAppEui;

/*!
 * AES encryption/decryption cipher application key
 */
static uint8_t *LoRaMacAppKey;

/*!
 * AES encryption/decryption cipher network session key
 */
static uint8_t LoRaMacNwkSKey[] =
{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/*!
 * AES encryption/decryption cipher application session key
 */
static uint8_t LoRaMacAppSKey[] =
{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/*!
 * Device nonce is a random value extracted by issuing a sequence of RSSI
 * measurements
 */
static uint16_t LoRaMacDevNonce;

/*!
 * Network ID ( 3 bytes )
 */
static uint32_t LoRaMacNetID;

/*!
 * Mote Address
 */
static uint32_t LoRaMacDevAddr;

/*!
 * Multicast channels linked list
 */
static MulticastParams_t *MulticastChannels = NULL;

/*!
 * Actual device class
 */
static DeviceClass_t LoRaMacDeviceClass;

/*!
 * Indicates if the node is connected to a private or public network
 */
static bool PublicNetwork;

/*!
 * Indicates if the node supports repeaters
 */
static bool RepeaterSupport;

/*!
 * Buffer containing the data to be sent or received.
 */
static uint8_t LoRaMacBuffer[LORAMAC_PHY_MAXPAYLOAD];

/*!
 * Length of packet in LoRaMacBuffer
 */
static uint16_t LoRaMacBufferPktLen = 0;

/*!
 * Length of the payload in LoRaMacBuffer
 */
static uint8_t LoRaMacTxPayloadLen = 0;

/*!
 * Buffer containing the upper layer data.
 */
static uint8_t LoRaMacPayload[LORAMAC_PHY_MAXPAYLOAD];
static uint8_t LoRaMacRxPayload[LORAMAC_PHY_MAXPAYLOAD];

/*!
 * LoRaMAC frame counter. Each time a packet is sent the counter is incremented.
 * Only the 16 LSB bits are sent
 */
static uint32_t UpLinkCounter = 0;

/*!
 * LoRaMAC frame counter. Each time a packet is received the counter is incremented.
 * Only the 16 LSB bits are received
 */
static uint32_t DownLinkCounter = 0;

/*!
 * IsPacketCounterFixed enables the MIC field tests by fixing the
 * UpLinkCounter value
 */
static bool IsUpLinkCounterFixed = false;

/*!
 * Used for test purposes. Disables the opening of the reception windows.
 */
static bool IsRxWindowsEnabled = true;

/*!
 * Indicates if the MAC layer has already joined a network.
 */
static bool IsLoRaMacNetworkJoined = false;

/*!
 * LoRaMac ADR control status
 */
static bool AdrCtrlOn = false;

/*!
 * Counts the number of missed ADR acknowledgements
 */
static uint32_t AdrAckCounter = 0;

/*!
 * If the node has sent a FRAME_TYPE_DATA_CONFIRMED_UP this variable indicates
 * if the nodes needs to manage the server acknowledgement.
 */
static bool NodeAckRequested = false;

/*!
 * If the server has sent a FRAME_TYPE_DATA_CONFIRMED_DOWN this variable indicates
 * if the ACK bit must be set for the next transmission
 */
static bool SrvAckRequested = false;

/*!
 * Indicates if the MAC layer wants to send MAC commands
 */
static bool MacCommandsInNextTx = false;

/*!
 * Contains the current MacCommandsBuffer index
 */
static uint8_t MacCommandsBufferIndex = 0;

/*!
 * Contains the current MacCommandsBuffer index for MAC commands to repeat
 */
static uint8_t MacCommandsBufferToRepeatIndex = 0;

/*!
 * Buffer containing the MAC layer commands
 */
static uint8_t MacCommandsBuffer[LORA_MAC_COMMAND_MAX_LENGTH];

/*!
 * Buffer containing the MAC layer commands which must be repeated
 */
static uint8_t MacCommandsBufferToRepeat[LORA_MAC_COMMAND_MAX_LENGTH];

/*!
 * LoRaMac parameters
 */
LoRaMacParams_t LoRaMacParams;

/*!
 * LoRaMac default parameters
 */
LoRaMacParams_t LoRaMacParamsDefaults;

/*!
 * Uplink messages repetitions counter
 */
static uint8_t ChannelsNbRepCounter = 0;

/*!
 * Maximum duty cycle
 * \remark Possibility to shutdown the device.
 */
static uint8_t MaxDCycle = 0;

/*!
 * Aggregated duty cycle management
 */
static uint16_t AggregatedDCycle;
static TimerTime_t AggregatedLastTxDoneTime;
static TimerTime_t AggregatedTimeOff;

/*!
 * Enables/Disables duty cycle management (Test only)
 */
static bool DutyCycleOn;

/*!
 * Current channel index
 */
static uint8_t Channel;

/*!
 * Stores the time at LoRaMac initialization.
 *
 * \remark Used for the BACKOFF_DC computation.
 */
static TimerTime_t LoRaMacInitializationTime = 0;

/*!
 * LoRaMac internal states
 */
enum eLoRaMacState
{
    LORAMAC_IDLE          = 0x00000000,
    LORAMAC_TX_RUNNING    = 0x00000001,
    LORAMAC_RX            = 0x00000002,
    LORAMAC_ACK_REQ       = 0x00000004,
    LORAMAC_ACK_RETRY     = 0x00000008,
    LORAMAC_TX_DELAYED    = 0x00000010,
    LORAMAC_TX_CONFIG     = 0x00000020,
    LORAMAC_RX_ABORT      = 0x00000040,
};

/*!
 * LoRaMac internal state
 */
uint32_t LoRaMacState = LORAMAC_IDLE;

/*!
 * LoRaMac timer used to check the LoRaMacState (runs every second)
 */
static TimerEvent_t MacStateCheckTimer;

/*!
 * LoRaMac upper layer event functions
 */
static LoRaMacPrimitives_t *LoRaMacPrimitives;

/*!
 * LoRaMac upper layer callback functions
 */
static LoRaMacCallback_t *LoRaMacCallbacks;

/*!
 * Radio events function pointer
 */
static RadioEvents_t RadioEvents;

/*!
 * LoRaMac duty cycle delayed Tx timer
 */
static TimerEvent_t TxDelayedTimer;

/*!
 * LoRaMac reception windows timers
 */
static TimerEvent_t RxWindowTimer1;
static TimerEvent_t RxWindowTimer2;

/*!
 * LoRaMac reception windows delay
 * \remark normal frame: RxWindowXDelay = ReceiveDelayX - RADIO_WAKEUP_TIME
 *         join frame  : RxWindowXDelay = JoinAcceptDelayX - RADIO_WAKEUP_TIME
 */
static uint32_t RxWindow1Delay;
static uint32_t RxWindow2Delay;

/*!
 * Acknowledge timeout timer. Used for packet retransmissions.
 */
static TimerEvent_t AckTimeoutTimer;

/*!
 * Number of trials to get a frame acknowledged
 */
static uint8_t AckTimeoutRetries = 1;

/*!
 * Number of trials to get a frame acknowledged
 */
static uint8_t AckTimeoutRetriesCounter = 1;

/*!
 * Indicates if the AckTimeout timer has expired or not
 */
static bool AckTimeoutRetry = false;

/*!
 * Last transmission time on air
 */
TimerTime_t TxTimeOnAir = 0;

/*!
 * Number of trials for the Join Request
 */
static uint8_t JoinRequestTrials;

/*!
 * Maximum number of trials for the Join Request
 */
static uint8_t MaxJoinRequestTrials;

/*!
 * Structure to hold an MCPS indication data.
 */
static McpsIndication_t McpsIndication;

/*!
 * Structure to hold MCPS confirm data.
 */
static McpsConfirm_t McpsConfirm;

/*!
 * Structure to hold MLME confirm data.
 */
static MlmeConfirm_t MlmeConfirm;

/*!
 * Holds the current rx window slot
 */
static uint8_t RxSlot = 0;

/*!
 * LoRaMac tx/rx operation state
 */
LoRaMacFlags_t LoRaMacFlags;

/*!
 * \brief Function to be executed on Radio Tx Done event
 */
static void OnRadioTxDone( void );

/*!
 * \brief This function prepares the MAC to abort the execution of function
 *        OnRadioRxDone in case of a reception error.
 */
static void PrepareRxDoneAbort( void );

/*!
 * \brief Function to be executed on Radio Rx Done event
 */
static void OnRadioRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr );

/*!
 * \brief Function executed on Radio Tx Timeout event
 */
static void OnRadioTxTimeout( void );

/*!
 * \brief Function executed on Radio Rx error event
 */
static void OnRadioRxError( void );

/*!
 * \brief Function executed on Radio Rx Timeout event
 */
static void OnRadioRxTimeout( void );

/*!
 * \brief Function executed on Resend Frame timer event.
 */
static void OnMacStateCheckTimerEvent( void );

/*!
 * \brief Function executed on duty cycle delayed Tx  timer event
 */
static void OnTxDelayedTimerEvent( void );

/*!
 * \brief Function executed on first Rx window timer event
 */
static void OnRxWindow1TimerEvent( void );

/*!
 * \brief Function executed on second Rx window timer event
 */
static void OnRxWindow2TimerEvent( void );

/*!
 * \brief Function executed on AckTimeout timer event
 */
static void OnAckTimeoutTimerEvent( void );

/*!
 * \brief Searches and set the next random available channel
 *
 * \param [OUT] Time to wait for the next transmission according to the duty
 *              cycle.
 *
 * \retval status  Function status [1: OK, 0: Unable to find a channel on the
 *                                  current datarate]
 */
static bool SetNextChannel( TimerTime_t* time );

/*!
 * \brief Initializes and opens the reception window
 *
 * \param [IN] freq window channel frequency
 * \param [IN] datarate window channel datarate
 * \param [IN] bandwidth window channel bandwidth
 * \param [IN] timeout window channel timeout
 *
 * \retval status Operation status [true: Success, false: Fail]
 */
static bool RxWindowSetup( uint32_t freq, int8_t datarate, uint32_t bandwidth, uint16_t timeout, bool rxContinuous );

/*!
 * \brief Verifies if the RX window 2 frequency is in range
 *
 * \param [IN] freq window channel frequency
 *
 * \retval status  Function status [1: OK, 0: Frequency not applicable]
 */
static bool Rx2FreqInRange( uint32_t freq );

/*!
 * \brief Adds a new MAC command to be sent.
 *
 * \Remark MAC layer internal function
 *
 * \param [in] cmd MAC command to be added
 *                 [MOTE_MAC_LINK_CHECK_REQ,
 *                  MOTE_MAC_LINK_ADR_ANS,
 *                  MOTE_MAC_DUTY_CYCLE_ANS,
 *                  MOTE_MAC_RX2_PARAM_SET_ANS,
 *                  MOTE_MAC_DEV_STATUS_ANS
 *                  MOTE_MAC_NEW_CHANNEL_ANS]
 * \param [in] p1  1st parameter ( optional depends on the command )
 * \param [in] p2  2nd parameter ( optional depends on the command )
 *
 * \retval status  Function status [0: OK, 1: Unknown command, 2: Buffer full]
 */
static LoRaMacStatus_t AddMacCommand( uint8_t cmd, uint8_t p1, uint8_t p2 );

/*!
 * \brief Parses the MAC commands which must be repeated.
 *
 * \Remark MAC layer internal function
 *
 * \param [IN] cmdBufIn  Buffer which stores the MAC commands to send
 * \param [IN] length  Length of the input buffer to parse
 * \param [OUT] cmdBufOut  Buffer which stores the MAC commands which must be
 *                         repeated.
 *
 * \retval Size of the MAC commands to repeat.
 */
static uint8_t ParseMacCommandsToRepeat( uint8_t* cmdBufIn, uint8_t length, uint8_t* cmdBufOut );

/*!
 * \brief Validates if the payload fits into the frame, taking the datarate
 *        into account.
 *
 * \details Refer to chapter 4.3.2 of the LoRaWAN specification, v1.0
 *
 * \param lenN Length of the application payload. The length depends on the
 *             datarate and is region specific
 *
 * \param datarate Current datarate
 *
 * \param fOptsLen Length of the fOpts field
 *
 * \retval [false: payload does not fit into the frame, true: payload fits into
 *          the frame]
 */
static bool ValidatePayloadLength( uint8_t lenN, int8_t datarate, uint8_t fOptsLen );

/*!
 * \brief Counts the number of bits in a mask.
 *
 * \param [IN] mask A mask from which the function counts the active bits.
 * \param [IN] nbBits The number of bits to check.
 *
 * \retval Number of enabled bits in the mask.
 */
static uint8_t CountBits( uint16_t mask, uint8_t nbBits );

#if defined( USE_BAND_915 ) || defined( USE_BAND_915_HYBRID )
/*!
 * \brief Counts the number of enabled 125 kHz channels in the channel mask.
 *        This function can only be applied to US915 band.
 *
 * \param [IN] channelsMask Pointer to the first element of the channel mask
 *
 * \retval Number of enabled channels in the channel mask
 */
static uint8_t CountNbEnabled125kHzChannels( uint16_t *channelsMask );

#if defined( USE_BAND_915_HYBRID )
/*!
 * \brief Validates the correctness of the channel mask for US915, hybrid mode.
 *
 * \param [IN] mask Block definition to set.
 * \param [OUT] channelsMask Pointer to the first element of the channel mask
 */
static void ReenableChannels( uint16_t mask, uint16_t* channelsMask );

/*!
 * \brief Validates the correctness of the channel mask for US915, hybrid mode.
 *
 * \param [IN] channelsMask Pointer to the first element of the channel mask
 *
 * \retval [true: channel mask correct, false: channel mask not correct]
 */
static bool ValidateChannelMask( uint16_t* channelsMask );
#endif

#endif

/*!
 * \brief Validates the correctness of the datarate against the enable channels.
 *
 * \param [IN] datarate Datarate to be check
 * \param [IN] channelsMask Pointer to the first element of the channel mask
 *
 * \retval [true: datarate can be used, false: datarate can not be used]
 */
static bool ValidateDatarate( int8_t datarate, uint16_t* channelsMask );

/*!
 * \brief Limits the Tx power according to the number of enabled channels
 *
 * \param [IN] txPower txPower to limit
 * \param [IN] maxBandTxPower Maximum band allowed TxPower
 *
 * \retval Returns the maximum valid tx power
 */
static int8_t LimitTxPower( int8_t txPower, int8_t maxBandTxPower );

/*!
 * \brief Verifies, if a value is in a given range.
 *
 * \param value Value to verify, if it is in range
 *
 * \param min Minimum possible value
 *
 * \param max Maximum possible value
 *
 * \retval Returns the maximum valid tx power
 */
static bool ValueInRange( int8_t value, int8_t min, int8_t max );

/*!
 * \brief Calculates the next datarate to set, when ADR is on or off
 *
 * \param [IN] adrEnabled Specify whether ADR is on or off
 *
 * \param [IN] updateChannelMask Set to true, if the channel masks shall be updated
 *
 * \param [OUT] datarateOut Reports the datarate which will be used next
 *
 * \retval Returns the state of ADR ack request
 */
static bool AdrNextDr( bool adrEnabled, bool updateChannelMask, int8_t* datarateOut );

/*!
 * \brief Disables channel in a specified channel mask
 *
 * \param [IN] id - Id of the channel
 *
 * \param [IN] mask - Pointer to the channel mask to edit
 *
 * \retval [true, if disable was successful, false if not]
 */
static bool DisableChannelInMask( uint8_t id, uint16_t* mask );

/*!
 * \brief Decodes MAC commands in the fOpts field and in the payload
 */
static void ProcessMacCommands( uint8_t *payload, uint8_t macIndex, uint8_t commandsSize, uint8_t snr );

/*!
 * \brief LoRaMAC layer generic send frame
 *
 * \param [IN] macHdr      MAC header field
 * \param [IN] fPort       MAC payload port
 * \param [IN] fBuffer     MAC data buffer to be sent
 * \param [IN] fBufferSize MAC data buffer size
 * \retval status          Status of the operation.
 */
LoRaMacStatus_t Send( LoRaMacHeader_t *macHdr, uint8_t fPort, void *fBuffer, uint16_t fBufferSize );

/*!
 * \brief LoRaMAC layer frame buffer initialization
 *
 * \param [IN] macHdr      MAC header field
 * \param [IN] fCtrl       MAC frame control field
 * \param [IN] fOpts       MAC commands buffer
 * \param [IN] fPort       MAC payload port
 * \param [IN] fBuffer     MAC data buffer to be sent
 * \param [IN] fBufferSize MAC data buffer size
 * \retval status          Status of the operation.
 */
LoRaMacStatus_t PrepareFrame( LoRaMacHeader_t *macHdr, LoRaMacFrameCtrl_t *fCtrl, uint8_t fPort, void *fBuffer, uint16_t fBufferSize );

/*
 * \brief Schedules the frame according to the duty cycle
 *
 * \retval Status of the operation
 */
static LoRaMacStatus_t ScheduleTx( void );

/*
 * \brief Calculates the back-off time for the band of a channel.
 *
 * \param [IN] channel     The last Tx channel index
 */
static void CalculateBackOff( uint8_t channel );

/*!
 * \brief LoRaMAC layer prepared frame buffer transmission with channel specification
 *
 * \remark PrepareFrame must be called at least once before calling this
 *         function.
 *
 * \param [IN] channel     Channel to transmit on
 * \retval status          Status of the operation.
 */
LoRaMacStatus_t SendFrameOnChannel( uint8_t channel );

/*!
 * \brief Sets the radio in continuous transmission mode
 *
 * \remark Uses the radio parameters set on the previous transmission.
 *
 * \param [IN] timeout     Time in seconds while the radio is kept in continuous wave mode
 * \retval status          Status of the operation.
 */
LoRaMacStatus_t SetTxContinuousWave( uint16_t timeout );

/*!
 * \brief Resets MAC specific parameters to default
 */
static void ResetMacParameters( void );

static void OnRadioTxDone( void )
{
    TimerTime_t curTime = TimerGetCurrentTime( );

    if( LoRaMacDeviceClass != CLASS_C )
    {
        Radio.Sleep( );
    }
    else
    {
        OnRxWindow2TimerEvent( );
    }

    // Setup timers
    if( IsRxWindowsEnabled == true )
    {
        TimerSetValue( &RxWindowTimer1, RxWindow1Delay );
        TimerStart( &RxWindowTimer1 );
        if( LoRaMacDeviceClass != CLASS_C )
        {
            TimerSetValue( &RxWindowTimer2, RxWindow2Delay );
            TimerStart( &RxWindowTimer2 );
        }
        if( ( LoRaMacDeviceClass == CLASS_C ) || ( NodeAckRequested == true ) )
        {
            TimerSetValue( &AckTimeoutTimer, RxWindow2Delay + ACK_TIMEOUT +
                                             randr( -ACK_TIMEOUT_RND, ACK_TIMEOUT_RND ) );
            TimerStart( &AckTimeoutTimer );
        }
    }
    else
    {
        McpsConfirm.Status = LORAMAC_EVENT_INFO_STATUS_OK;
        MlmeConfirm.Status = LORAMAC_EVENT_INFO_STATUS_RX2_TIMEOUT;

        if( LoRaMacFlags.Value == 0 )
        {
            LoRaMacFlags.Bits.McpsReq = 1;
        }
        LoRaMacFlags.Bits.MacDone = 1;
    }

    // Update last tx done time for the current channel
    Bands[Channels[Channel].Band].LastTxDoneTime = curTime;
    // Update Aggregated last tx done time
    AggregatedLastTxDoneTime = curTime;
    // Update Backoff
    CalculateBackOff( Channel );

    if( NodeAckRequested == false )
    {
        McpsConfirm.Status = LORAMAC_EVENT_INFO_STATUS_OK;
        ChannelsNbRepCounter++;
    }
}

static void PrepareRxDoneAbort( void )
{
    LoRaMacState |= LORAMAC_RX_ABORT;

    if( NodeAckRequested )
    {
        OnAckTimeoutTimerEvent( );
    }

    if( ( RxSlot == 0 ) && ( LoRaMacDeviceClass == CLASS_C ) )
    {
        OnRxWindow2TimerEvent( );
    }

    LoRaMacFlags.Bits.McpsInd = 1;
    LoRaMacFlags.Bits.MacDone = 1;

    // Trig OnMacCheckTimerEvent call as soon as possible
    TimerSetValue( &MacStateCheckTimer, 1 );
    TimerStart( &MacStateCheckTimer );
}

static void OnRadioRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr )
{
    LoRaMacHeader_t macHdr;
    LoRaMacFrameCtrl_t fCtrl;
    bool skipIndication = false;

    uint8_t pktHeaderLen = 0;
    uint32_t address = 0;
    uint8_t appPayloadStartIndex = 0;
    uint8_t port = 0xFF;
    uint8_t frameLen = 0;
    uint32_t mic = 0;
    uint32_t micRx = 0;

    uint16_t sequenceCounter = 0;
    uint16_t sequenceCounterPrev = 0;
    uint16_t sequenceCounterDiff = 0;
    uint32_t downLinkCounter = 0;

    MulticastParams_t *curMulticastParams = NULL;
    uint8_t *nwkSKey = LoRaMacNwkSKey;
    uint8_t *appSKey = LoRaMacAppSKey;

    uint8_t multicast = 0;

    bool isMicOk = false;

    McpsConfirm.AckReceived = false;
    McpsIndication.Rssi = rssi;
    McpsIndication.Snr = snr;
    McpsIndication.RxSlot = RxSlot;
    McpsIndication.Port = 0;
    McpsIndication.Multicast = 0;
    McpsIndication.FramePending = 0;
    McpsIndication.Buffer = NULL;
    McpsIndication.BufferSize = 0;
    McpsIndication.RxData = false;
    McpsIndication.AckReceived = false;
    McpsIndication.DownLinkCounter = 0;
    McpsIndication.McpsIndication = MCPS_UNCONFIRMED;

    if( LoRaMacDeviceClass != CLASS_C )
    {
        Radio.Sleep( );
    }
    TimerStop( &RxWindowTimer2 );

    macHdr.Value = payload[pktHeaderLen++];

    switch( macHdr.Bits.MType )
    {
        case FRAME_TYPE_JOIN_ACCEPT:
            if( IsLoRaMacNetworkJoined == true )
            {
                McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_ERROR;
                PrepareRxDoneAbort( );
                return;
            }
            LoRaMacJoinDecrypt( payload + 1, size - 1, LoRaMacAppKey, LoRaMacRxPayload + 1 );

            LoRaMacRxPayload[0] = macHdr.Value;

            LoRaMacJoinComputeMic( LoRaMacRxPayload, size - LORAMAC_MFR_LEN, LoRaMacAppKey, &mic );

            micRx |= ( uint32_t )LoRaMacRxPayload[size - LORAMAC_MFR_LEN];
            micRx |= ( ( uint32_t )LoRaMacRxPayload[size - LORAMAC_MFR_LEN + 1] << 8 );
            micRx |= ( ( uint32_t )LoRaMacRxPayload[size - LORAMAC_MFR_LEN + 2] << 16 );
            micRx |= ( ( uint32_t )LoRaMacRxPayload[size - LORAMAC_MFR_LEN + 3] << 24 );

            if( micRx == mic )
            {
                LoRaMacJoinComputeSKeys( LoRaMacAppKey, LoRaMacRxPayload + 1, LoRaMacDevNonce, LoRaMacNwkSKey, LoRaMacAppSKey );

                LoRaMacNetID = ( uint32_t )LoRaMacRxPayload[4];
                LoRaMacNetID |= ( ( uint32_t )LoRaMacRxPayload[5] << 8 );
                LoRaMacNetID |= ( ( uint32_t )LoRaMacRxPayload[6] << 16 );

                LoRaMacDevAddr = ( uint32_t )LoRaMacRxPayload[7];
                LoRaMacDevAddr |= ( ( uint32_t )LoRaMacRxPayload[8] << 8 );
                LoRaMacDevAddr |= ( ( uint32_t )LoRaMacRxPayload[9] << 16 );
                LoRaMacDevAddr |= ( ( uint32_t )LoRaMacRxPayload[10] << 24 );

                // DLSettings
                LoRaMacParams.Rx1DrOffset = ( LoRaMacRxPayload[11] >> 4 ) & 0x07;
                LoRaMacParams.Rx2Channel.Datarate = LoRaMacRxPayload[11] & 0x0F;

                // RxDelay
                LoRaMacParams.ReceiveDelay1 = ( LoRaMacRxPayload[12] & 0x0F );
                if( LoRaMacParams.ReceiveDelay1 == 0 )
                {
                    LoRaMacParams.ReceiveDelay1 = 1;
                }
                LoRaMacParams.ReceiveDelay1 *= 1e3;
                LoRaMacParams.ReceiveDelay2 = LoRaMacParams.ReceiveDelay1 + 1e3;

#if !( defined( USE_BAND_915 ) || defined( USE_BAND_915_HYBRID ) )
                //CFList
                if( ( size - 1 ) > 16 )
                {
                    ChannelParams_t param;
                    param.DrRange.Value = ( DR_5 << 4 ) | DR_0;

                    LoRaMacState |= LORAMAC_TX_CONFIG;
                    for( uint8_t i = 3, j = 0; i < ( 5 + 3 ); i++, j += 3 )
                    {
                        param.Frequency = ( ( uint32_t )LoRaMacRxPayload[13 + j] | ( ( uint32_t )LoRaMacRxPayload[14 + j] << 8 ) | ( ( uint32_t )LoRaMacRxPayload[15 + j] << 16 ) ) * 100;
                        if( param.Frequency != 0 )
                        {
                            LoRaMacChannelAdd( i, param );
                        }
                        else
                        {
                            LoRaMacChannelRemove( i );
                        }
                    }
                    LoRaMacState &= ~LORAMAC_TX_CONFIG;
                }
#endif
                MlmeConfirm.Status = LORAMAC_EVENT_INFO_STATUS_OK;
                IsLoRaMacNetworkJoined = true;
                LoRaMacParams.ChannelsDatarate = LoRaMacParamsDefaults.ChannelsDatarate;
            }
            else
            {
                MlmeConfirm.Status = LORAMAC_EVENT_INFO_STATUS_JOIN_FAIL;
            }
            break;
        case FRAME_TYPE_DATA_CONFIRMED_DOWN:
        case FRAME_TYPE_DATA_UNCONFIRMED_DOWN:
            {
                address = payload[pktHeaderLen++];
                address |= ( (uint32_t)payload[pktHeaderLen++] << 8 );
                address |= ( (uint32_t)payload[pktHeaderLen++] << 16 );
                address |= ( (uint32_t)payload[pktHeaderLen++] << 24 );

                if( address != LoRaMacDevAddr )
                {
                    curMulticastParams = MulticastChannels;
                    while( curMulticastParams != NULL )
                    {
                        if( address == curMulticastParams->Address )
                        {
                            multicast = 1;
                            nwkSKey = curMulticastParams->NwkSKey;
                            appSKey = curMulticastParams->AppSKey;
                            downLinkCounter = curMulticastParams->DownLinkCounter;
                            break;
                        }
                        curMulticastParams = curMulticastParams->Next;
                    }
                    if( multicast == 0 )
                    {
                        // We are not the destination of this frame.
                        McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_ADDRESS_FAIL;
                        PrepareRxDoneAbort( );
                        return;
                    }
                }
                else
                {
                    multicast = 0;
                    nwkSKey = LoRaMacNwkSKey;
                    appSKey = LoRaMacAppSKey;
                    downLinkCounter = DownLinkCounter;
                }

                fCtrl.Value = payload[pktHeaderLen++];

                sequenceCounter = ( uint16_t )payload[pktHeaderLen++];
                sequenceCounter |= ( uint16_t )payload[pktHeaderLen++] << 8;

                appPayloadStartIndex = 8 + fCtrl.Bits.FOptsLen;

                micRx |= ( uint32_t )payload[size - LORAMAC_MFR_LEN];
                micRx |= ( ( uint32_t )payload[size - LORAMAC_MFR_LEN + 1] << 8 );
                micRx |= ( ( uint32_t )payload[size - LORAMAC_MFR_LEN + 2] << 16 );
                micRx |= ( ( uint32_t )payload[size - LORAMAC_MFR_LEN + 3] << 24 );

                sequenceCounterPrev = ( uint16_t )downLinkCounter;
                sequenceCounterDiff = ( sequenceCounter - sequenceCounterPrev );

                if( sequenceCounterDiff < ( 1 << 15 ) )
                {
                    downLinkCounter += sequenceCounterDiff;
                    LoRaMacComputeMic( payload, size - LORAMAC_MFR_LEN, nwkSKey, address, DOWN_LINK, downLinkCounter, &mic );
                    if( micRx == mic )
                    {
                        isMicOk = true;
                    }
                }
                else
                {
                    // check for sequence roll-over
                    uint32_t  downLinkCounterTmp = downLinkCounter + 0x10000 + ( int16_t )sequenceCounterDiff;
                    LoRaMacComputeMic( payload, size - LORAMAC_MFR_LEN, nwkSKey, address, DOWN_LINK, downLinkCounterTmp, &mic );
                    if( micRx == mic )
                    {
                        isMicOk = true;
                        downLinkCounter = downLinkCounterTmp;
                    }
                }

                // Check for a the maximum allowed counter difference
                if( sequenceCounterDiff >= MAX_FCNT_GAP )
                {
                    McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_DOWNLINK_TOO_MANY_FRAMES_LOSS;
                    McpsIndication.DownLinkCounter = downLinkCounter;
                    PrepareRxDoneAbort( );
                    return;
                }

                if( isMicOk == true )
                {
                    McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_OK;
                    McpsIndication.Multicast = multicast;
                    McpsIndication.FramePending = fCtrl.Bits.FPending;
                    McpsIndication.Buffer = NULL;
                    McpsIndication.BufferSize = 0;
                    McpsIndication.DownLinkCounter = downLinkCounter;

                    McpsConfirm.Status = LORAMAC_EVENT_INFO_STATUS_OK;

                    AdrAckCounter = 0;
                    MacCommandsBufferToRepeatIndex = 0;

                    // Update 32 bits downlink counter
                    if( multicast == 1 )
                    {
                        McpsIndication.McpsIndication = MCPS_MULTICAST;

                        if( ( curMulticastParams->DownLinkCounter == downLinkCounter ) &&
                            ( curMulticastParams->DownLinkCounter != 0 ) )
                        {
                            McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_DOWNLINK_REPEATED;
                            McpsIndication.DownLinkCounter = downLinkCounter;
                            PrepareRxDoneAbort( );
                            return;
                        }
                        curMulticastParams->DownLinkCounter = downLinkCounter;
                    }
                    else
                    {
                        if( macHdr.Bits.MType == FRAME_TYPE_DATA_CONFIRMED_DOWN )
                        {
                            SrvAckRequested = true;
                            McpsIndication.McpsIndication = MCPS_CONFIRMED;

                            if( ( DownLinkCounter == downLinkCounter ) &&
                                ( DownLinkCounter != 0 ) )
                            {
                                // Duplicated confirmed downlink. Skip indication.
                                skipIndication = true;
                            }
                        }
                        else
                        {
                            SrvAckRequested = false;
                            McpsIndication.McpsIndication = MCPS_UNCONFIRMED;

                            if( ( DownLinkCounter == downLinkCounter ) &&
                                ( DownLinkCounter != 0 ) )
                            {
                                McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_DOWNLINK_REPEATED;
                                McpsIndication.DownLinkCounter = downLinkCounter;
                                PrepareRxDoneAbort( );
                                return;
                            }
                        }
                        DownLinkCounter = downLinkCounter;
                    }

                    if( ( ( size - 4 ) - appPayloadStartIndex ) > 0 )
                    {
                        port = payload[appPayloadStartIndex++];
                        frameLen = ( size - 4 ) - appPayloadStartIndex;

                        McpsIndication.Port = port;

                        if( port == 0 )
                        {
                            if( fCtrl.Bits.FOptsLen == 0 )
                            {
                                LoRaMacPayloadDecrypt( payload + appPayloadStartIndex,
                                                       frameLen,
                                                       nwkSKey,
                                                       address,
                                                       DOWN_LINK,
                                                       downLinkCounter,
                                                       LoRaMacRxPayload );

                                // Decode frame payload MAC commands
                                ProcessMacCommands( LoRaMacRxPayload, 0, frameLen, snr );
                            }
                            else
                            {
                                skipIndication = true;
                            }
                        }
                        else
                        {
                            if( fCtrl.Bits.FOptsLen > 0 )
                            {
                                // Decode Options field MAC commands. Omit the fPort.
                                ProcessMacCommands( payload, 8, appPayloadStartIndex - 1, snr );
                            }

                            LoRaMacPayloadDecrypt( payload + appPayloadStartIndex,
                                                   frameLen,
                                                   appSKey,
                                                   address,
                                                   DOWN_LINK,
                                                   downLinkCounter,
                                                   LoRaMacRxPayload );

                            if( skipIndication == false )
                            {
                                McpsIndication.Buffer = LoRaMacRxPayload;
                                McpsIndication.BufferSize = frameLen;
                                McpsIndication.RxData = true;
                            }
                        }
                    }
                    else
                    {
                        if( fCtrl.Bits.FOptsLen > 0 )
                        {
                            // Decode Options field MAC commands
                            ProcessMacCommands( payload, 8, appPayloadStartIndex, snr );
                        }
                    }

                    if( skipIndication == false )
                    {
                        // Check if the frame is an acknowledgement
                        if( fCtrl.Bits.Ack == 1 )
                        {
                            McpsConfirm.AckReceived = true;
                            McpsIndication.AckReceived = true;

                            // Stop the AckTimeout timer as no more retransmissions
                            // are needed.
                            TimerStop( &AckTimeoutTimer );
                        }
                        else
                        {
                            McpsConfirm.AckReceived = false;

                            if( AckTimeoutRetriesCounter > AckTimeoutRetries )
                            {
                                // Stop the AckTimeout timer as no more retransmissions
                                // are needed.
                                TimerStop( &AckTimeoutTimer );
                            }
                        }
                        LoRaMacFlags.Bits.McpsInd = 1;
                    }
                }
                else
                {
                    McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_MIC_FAIL;

                    PrepareRxDoneAbort( );
                    return;
                }
            }
            break;
        case FRAME_TYPE_PROPRIETARY:
            {
                memcpy1( LoRaMacRxPayload, &payload[pktHeaderLen], size );

                McpsIndication.McpsIndication = MCPS_PROPRIETARY;
                McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_OK;
                McpsIndication.Buffer = LoRaMacRxPayload;
                McpsIndication.BufferSize = size - pktHeaderLen;

                LoRaMacFlags.Bits.McpsInd = 1;
                break;
            }
        default:
            McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_ERROR;
            PrepareRxDoneAbort( );
            break;
    }

    if( ( RxSlot == 0 ) && ( LoRaMacDeviceClass == CLASS_C ) )
    {
        OnRxWindow2TimerEvent( );
    }
    LoRaMacFlags.Bits.MacDone = 1;

    // Trig OnMacCheckTimerEvent call as soon as possible
    TimerSetValue( &MacStateCheckTimer, 1 );
    TimerStart( &MacStateCheckTimer );
}

static void OnRadioTxTimeout( void )
{
    if( LoRaMacDeviceClass != CLASS_C )
    {
        Radio.Sleep( );
    }
    else
    {
        OnRxWindow2TimerEvent( );
    }

    McpsConfirm.Status = LORAMAC_EVENT_INFO_STATUS_TX_TIMEOUT;
    MlmeConfirm.Status = LORAMAC_EVENT_INFO_STATUS_TX_TIMEOUT;
    LoRaMacFlags.Bits.MacDone = 1;
}

static void OnRadioRxError( void )
{
    if( LoRaMacDeviceClass != CLASS_C )
    {
        Radio.Sleep( );
    }
    else
    {
        OnRxWindow2TimerEvent( );
    }

    if( RxSlot == 1 )
    {
        if( NodeAckRequested == true )
        {
            McpsConfirm.Status = LORAMAC_EVENT_INFO_STATUS_RX2_ERROR;
        }
        MlmeConfirm.Status = LORAMAC_EVENT_INFO_STATUS_RX2_ERROR;
        LoRaMacFlags.Bits.MacDone = 1;
    }
}

static void OnRadioRxTimeout( void )
{
    if( LoRaMacDeviceClass != CLASS_C )
    {
        Radio.Sleep( );
    }
    else
    {
        OnRxWindow2TimerEvent( );
    }

    if( RxSlot == 1 )
    {
        if( NodeAckRequested == true )
        {
            McpsConfirm.Status = LORAMAC_EVENT_INFO_STATUS_RX2_TIMEOUT;
        }
        MlmeConfirm.Status = LORAMAC_EVENT_INFO_STATUS_RX2_TIMEOUT;
        LoRaMacFlags.Bits.MacDone = 1;
    }
}

static void OnMacStateCheckTimerEvent( void )
{
    TimerStop( &MacStateCheckTimer );
    bool txTimeout = false;

    if( LoRaMacFlags.Bits.MacDone == 1 )
    {
        if( ( LoRaMacState & LORAMAC_RX_ABORT ) == LORAMAC_RX_ABORT )
        {
            LoRaMacState &= ~LORAMAC_RX_ABORT;
            LoRaMacState &= ~LORAMAC_TX_RUNNING;
        }

        if( ( LoRaMacFlags.Bits.MlmeReq == 1 ) || ( ( LoRaMacFlags.Bits.McpsReq == 1 ) ) )
        {
            if( ( McpsConfirm.Status == LORAMAC_EVENT_INFO_STATUS_TX_TIMEOUT ) ||
                ( MlmeConfirm.Status == LORAMAC_EVENT_INFO_STATUS_TX_TIMEOUT ) )
            {
                // Stop transmit cycle due to tx timeout.
                LoRaMacState &= ~LORAMAC_TX_RUNNING;
                McpsConfirm.NbRetries = AckTimeoutRetriesCounter;
                McpsConfirm.AckReceived = false;
                McpsConfirm.TxTimeOnAir = 0;
                txTimeout = true;
            }
        }

        if( ( NodeAckRequested == false ) && ( txTimeout == false ) )
        {
            if( ( LoRaMacFlags.Bits.MlmeReq == 1 ) || ( ( LoRaMacFlags.Bits.McpsReq == 1 ) ) )
            {
                if( ( LoRaMacFlags.Bits.MlmeReq == 1 ) && ( MlmeConfirm.MlmeRequest == MLME_JOIN ) )
                {// Procedure for the join request
                    MlmeConfirm.NbRetries = JoinRequestTrials;

                    if( MlmeConfirm.Status == LORAMAC_EVENT_INFO_STATUS_OK )
                    {// Node joined successfully
                        UpLinkCounter = 0;
                        ChannelsNbRepCounter = 0;
                        LoRaMacState &= ~LORAMAC_TX_RUNNING;
                    }
                    else
                    {
                        if( ( JoinRequestTrials >= MaxJoinRequestTrials ) )
                        {
                            LoRaMacState &= ~LORAMAC_TX_RUNNING;
                        }
                        else
                        {
                            LoRaMacFlags.Bits.MacDone = 0;
                            // Sends the same frame again
                            OnTxDelayedTimerEvent( );
                        }
                    }
                }
                else
                {// Procedure for all other frames
                    if( ( ChannelsNbRepCounter >= LoRaMacParams.ChannelsNbRep ) || ( LoRaMacFlags.Bits.McpsInd == 1 ) )
                    {
                        ChannelsNbRepCounter = 0;

                        AdrAckCounter++;
                        if( IsUpLinkCounterFixed == false )
                        {
                            UpLinkCounter++;
                        }

                        LoRaMacState &= ~LORAMAC_TX_RUNNING;
                    }
                    else
                    {
                        LoRaMacFlags.Bits.MacDone = 0;
                        // Sends the same frame again
                        OnTxDelayedTimerEvent( );
                    }    
                }
            }
        }

        if( LoRaMacFlags.Bits.McpsInd == 1 )
        {// Procedure if we received a frame
            if( ( McpsConfirm.AckReceived == true ) || ( AckTimeoutRetriesCounter > AckTimeoutRetries ) )
            {
                AckTimeoutRetry = false;
                NodeAckRequested = false;
                if( IsUpLinkCounterFixed == false )
                {
                    UpLinkCounter++;
                }
                McpsConfirm.NbRetries = AckTimeoutRetriesCounter;

                LoRaMacState &= ~LORAMAC_TX_RUNNING;
            }
        }

        if( ( AckTimeoutRetry == true ) && ( ( LoRaMacState & LORAMAC_TX_DELAYED ) == 0 ) )
        {// Retransmissions procedure for confirmed uplinks
            AckTimeoutRetry = false;
            if( ( AckTimeoutRetriesCounter < AckTimeoutRetries ) && ( AckTimeoutRetriesCounter <= MAX_ACK_RETRIES ) )
            {
                AckTimeoutRetriesCounter++;

                if( ( AckTimeoutRetriesCounter % 2 ) == 1 )
                {
                    LoRaMacParams.ChannelsDatarate = MAX( LoRaMacParams.ChannelsDatarate - 1, LORAMAC_TX_MIN_DATARATE );
                }
                if( ValidatePayloadLength( LoRaMacTxPayloadLen, LoRaMacParams.ChannelsDatarate, MacCommandsBufferIndex ) == true )
                {
                    LoRaMacFlags.Bits.MacDone = 0;
                    // Sends the same frame again
                    ScheduleTx( );
                }
                else
                {
                    // The DR is not applicable for the payload size
                    McpsConfirm.Status = LORAMAC_EVENT_INFO_STATUS_TX_DR_PAYLOAD_SIZE_ERROR;

                    LoRaMacState &= ~LORAMAC_TX_RUNNING;
                    NodeAckRequested = false;
                    McpsConfirm.AckReceived = false;
                    McpsConfirm.NbRetries = AckTimeoutRetriesCounter;
                    McpsConfirm.Datarate = LoRaMacParams.ChannelsDatarate;
                    if( IsUpLinkCounterFixed == false )
                    {
                        UpLinkCounter++;
                    }
                }
            }
            else
            {
#if defined( USE_BAND_433 ) || defined( USE_BAND_780 ) || defined( USE_BAND_868 )
                // Re-enable default channels LC1, LC2, LC3
                LoRaMacParams.ChannelsMask[0] = LoRaMacParams.ChannelsMask[0] | ( LC( 1 ) + LC( 2 ) + LC( 3 ) );
#elif defined( USE_BAND_470 )
                // Re-enable default channels
                memcpy1( ( uint8_t* )LoRaMacParams.ChannelsMask, ( uint8_t* )LoRaMacParamsDefaults.ChannelsMask, sizeof( LoRaMacParams.ChannelsMask ) );
#elif defined( USE_BAND_915 )
                // Re-enable default channels
                memcpy1( ( uint8_t* )LoRaMacParams.ChannelsMask, ( uint8_t* )LoRaMacParamsDefaults.ChannelsMask, sizeof( LoRaMacParams.ChannelsMask ) );
#elif defined( USE_BAND_915_HYBRID )
                // Re-enable default channels
                ReenableChannels( LoRaMacParamsDefaults.ChannelsMask[4], LoRaMacParams.ChannelsMask );
#else
    #error "Please define a frequency band in the compiler options."
#endif
                LoRaMacState &= ~LORAMAC_TX_RUNNING;

                NodeAckRequested = false;
                McpsConfirm.AckReceived = false;
                McpsConfirm.NbRetries = AckTimeoutRetriesCounter;
                if( IsUpLinkCounterFixed == false )
                {
                    UpLinkCounter++;
                }
            }
        }
    }
    // Handle reception for Class B and Class C
    if( ( LoRaMacState & LORAMAC_RX ) == LORAMAC_RX )
    {
        LoRaMacState &= ~LORAMAC_RX;
    }
    if( LoRaMacState == LORAMAC_IDLE )
    {
        if( LoRaMacFlags.Bits.McpsReq == 1 )
        {
            LoRaMacPrimitives->MacMcpsConfirm( &McpsConfirm );
            LoRaMacFlags.Bits.McpsReq = 0;
        }

        if( LoRaMacFlags.Bits.MlmeReq == 1 )
        {
            LoRaMacPrimitives->MacMlmeConfirm( &MlmeConfirm );
            LoRaMacFlags.Bits.MlmeReq = 0;
        }

        LoRaMacFlags.Bits.MacDone = 0;
    }
    else
    {
        // Operation not finished restart timer
        TimerSetValue( &MacStateCheckTimer, MAC_STATE_CHECK_TIMEOUT );
        TimerStart( &MacStateCheckTimer );
    }

    if( LoRaMacFlags.Bits.McpsInd == 1 )
    {
        LoRaMacPrimitives->MacMcpsIndication( &McpsIndication );
        LoRaMacFlags.Bits.McpsInd = 0;
    }
}

static void OnTxDelayedTimerEvent( void )
{
    LoRaMacHeader_t macHdr;
    LoRaMacFrameCtrl_t fCtrl;
    AlternateDrParams_t altDr;

    TimerStop( &TxDelayedTimer );
    LoRaMacState &= ~LORAMAC_TX_DELAYED;

    if( ( LoRaMacFlags.Bits.MlmeReq == 1 ) && ( MlmeConfirm.MlmeRequest == MLME_JOIN ) )
    {
        ResetMacParameters( );

        altDr.NbTrials = JoinRequestTrials + 1;
        LoRaMacParams.ChannelsDatarate = RegionAlternateDr( LoRaMacRegion, &altDr );

        macHdr.Value = 0;
        macHdr.Bits.MType = FRAME_TYPE_JOIN_REQ;

        fCtrl.Value = 0;
        fCtrl.Bits.Adr = AdrCtrlOn;

        /* In case of join request retransmissions, the stack must prepare
         * the frame again, because the network server keeps track of the random
         * LoRaMacDevNonce values to prevent reply attacks. */
        PrepareFrame( &macHdr, &fCtrl, 0, NULL, 0 );
    }

    ScheduleTx( );
}

static void OnRxWindow1TimerEvent( void )
{
    uint16_t symbTimeout = 5; // DR_2, DR_1, DR_0
    int8_t datarate = 0;
    uint32_t bandwidth = 0; // LoRa 125 kHz

    TimerStop( &RxWindowTimer1 );
    RxSlot = 0;

    if( LoRaMacDeviceClass == CLASS_C )
    {
        Radio.Standby( );
    }

#if defined( USE_BAND_433 ) || defined( USE_BAND_780 ) || defined( USE_BAND_868 )
    datarate = LoRaMacParams.ChannelsDatarate - LoRaMacParams.Rx1DrOffset;
    if( datarate < 0 )
    {
        datarate = DR_0;
    }

    // For higher datarates, we increase the number of symbols generating a Rx Timeout
    if( ( datarate == DR_3 ) || ( datarate == DR_4 ) )
    { // DR_4, DR_3
        symbTimeout = 8;
    }
    else if( datarate == DR_5 )
    {
        symbTimeout = 10;
    }
    else if( datarate == DR_6 )
    {// LoRa 250 kHz
        bandwidth  = 1;
        symbTimeout = 14;
    }
    RxWindowSetup( Channels[Channel].Frequency, datarate, bandwidth, symbTimeout, false );
#elif defined( USE_BAND_470 )
    datarate = LoRaMacParams.ChannelsDatarate - LoRaMacParams.Rx1DrOffset;
    if( datarate < 0 )
    {
        datarate = DR_0;
    }

    // For higher datarates, we increase the number of symbols generating a Rx Timeout
    if( ( datarate == DR_3 ) || ( datarate == DR_4 ) )
    { // DR_4, DR_3
        symbTimeout = 8;
    }
    else if( datarate == DR_5 )
    {
        symbTimeout = 10;
    }
    RxWindowSetup( LORAMAC_FIRST_RX1_CHANNEL + ( Channel % 48 ) * LORAMAC_STEPWIDTH_RX1_CHANNEL, datarate, bandwidth, symbTimeout, false );
#elif ( defined( USE_BAND_915 ) || defined( USE_BAND_915_HYBRID ) )
    datarate = datarateOffsets[LoRaMacParams.ChannelsDatarate][LoRaMacParams.Rx1DrOffset];
    if( datarate < 0 )
    {
        datarate = DR_0;
    }
    // For higher datarates, we increase the number of symbols generating a Rx Timeout
    switch( datarate )
    {
        case DR_0:       // SF10 - BW125
            symbTimeout = 5;
            break;

        case DR_1:       // SF9  - BW125
        case DR_2:       // SF8  - BW125
        case DR_8:       // SF12 - BW500
        case DR_9:       // SF11 - BW500
        case DR_10:      // SF10 - BW500
            symbTimeout = 8;
            break;

        case DR_3:       // SF7  - BW125
        case DR_11:      // SF9  - BW500
            symbTimeout = 10;
            break;

        case DR_4:       // SF8  - BW500
        case DR_12:      // SF8  - BW500
            symbTimeout = 14;
            break;

        case DR_13:      // SF7  - BW500
            symbTimeout = 16;
            break;
        default:
            break;
    }
    if( datarate >= DR_4 )
    {// LoRa 500 kHz
        bandwidth  = 2;
    }
    RxWindowSetup( LORAMAC_FIRST_RX1_CHANNEL + ( Channel % 8 ) * LORAMAC_STEPWIDTH_RX1_CHANNEL, datarate, bandwidth, symbTimeout, false );
#else
    #error "Please define a frequency band in the compiler options."
#endif
}

static void OnRxWindow2TimerEvent( void )
{
    uint16_t symbTimeout = 5; // DR_2, DR_1, DR_0
    uint32_t bandwidth = 0; // LoRa 125 kHz
    bool rxContinuousMode = false;

    TimerStop( &RxWindowTimer2 );

#if defined( USE_BAND_433 ) || defined( USE_BAND_780 ) || defined( USE_BAND_868 )
    // For higher datarates, we increase the number of symbols generating a Rx Timeout
    if( ( LoRaMacParams.Rx2Channel.Datarate == DR_3 ) || ( LoRaMacParams.Rx2Channel.Datarate == DR_4 ) )
    { // DR_4, DR_3
        symbTimeout = 8;
    }
    else if( LoRaMacParams.Rx2Channel.Datarate == DR_5 )
    {
        symbTimeout = 10;
    }
    else if( LoRaMacParams.Rx2Channel.Datarate == DR_6 )
    {// LoRa 250 kHz
        bandwidth  = 1;
        symbTimeout = 14;
    }
#elif defined( USE_BAND_470 )
    // For higher datarates, we increase the number of symbols generating a Rx Timeout
    if( ( LoRaMacParams.Rx2Channel.Datarate == DR_3 ) || ( LoRaMacParams.Rx2Channel.Datarate == DR_4 ) )
    { // DR_4, DR_3
        symbTimeout = 8;
    }
    else if( LoRaMacParams.Rx2Channel.Datarate == DR_5 )
    {
        symbTimeout = 10;
    }
#elif ( defined( USE_BAND_915 ) || defined( USE_BAND_915_HYBRID ) )
    // For higher datarates, we increase the number of symbols generating a Rx Timeout
    switch( LoRaMacParams.Rx2Channel.Datarate )
    {
        case DR_0:       // SF10 - BW125
            symbTimeout = 5;
            break;

        case DR_1:       // SF9  - BW125
        case DR_2:       // SF8  - BW125
        case DR_8:       // SF12 - BW500
        case DR_9:       // SF11 - BW500
        case DR_10:      // SF10 - BW500
            symbTimeout = 8;
            break;

        case DR_3:       // SF7  - BW125
        case DR_11:      // SF9  - BW500
            symbTimeout = 10;
            break;

        case DR_4:       // SF8  - BW500
        case DR_12:      // SF8  - BW500
            symbTimeout = 14;
            break;

        case DR_13:      // SF7  - BW500
            symbTimeout = 16;
            break;
        default:
            break;
    }
    if( LoRaMacParams.Rx2Channel.Datarate >= DR_4 )
    {// LoRa 500 kHz
        bandwidth  = 2;
    }
#else
    #error "Please define a frequency band in the compiler options."
#endif
    if( LoRaMacDeviceClass == CLASS_C )
    {
        rxContinuousMode = true;
    }
    if( RxWindowSetup( LoRaMacParams.Rx2Channel.Frequency, LoRaMacParams.Rx2Channel.Datarate, bandwidth, symbTimeout, rxContinuousMode ) == true )
    {
        RxSlot = 1;
    }
}

static void OnAckTimeoutTimerEvent( void )
{
    TimerStop( &AckTimeoutTimer );

    if( NodeAckRequested == true )
    {
        AckTimeoutRetry = true;
        LoRaMacState &= ~LORAMAC_ACK_REQ;
    }
    if( LoRaMacDeviceClass == CLASS_C )
    {
        LoRaMacFlags.Bits.MacDone = 1;
    }
}

static bool SetNextChannel( TimerTime_t* time )
{
    uint8_t nbEnabledChannels = 0;
    uint8_t delayTx = 0;
    uint8_t enabledChannels[LORA_MAX_NB_CHANNELS];
    TimerTime_t nextTxDelay = ( TimerTime_t )( -1 );

    memset1( enabledChannels, 0, LORA_MAX_NB_CHANNELS );

#if defined( USE_BAND_915 ) || defined( USE_BAND_915_HYBRID )
    if( CountNbEnabled125kHzChannels( ChannelsMaskRemaining ) == 0 )
    { // Restore default channels
        memcpy1( ( uint8_t* ) ChannelsMaskRemaining, ( uint8_t* ) LoRaMacParams.ChannelsMask, 8 );
    }
    if( ( LoRaMacParams.ChannelsDatarate >= DR_4 ) && ( ( ChannelsMaskRemaining[4] & 0x00FF ) == 0 ) )
    { // Make sure, that the channels are activated
        ChannelsMaskRemaining[4] = LoRaMacParams.ChannelsMask[4];
    }
#elif defined( USE_BAND_470 )
    if( ( CountBits( LoRaMacParams.ChannelsMask[0], 16 ) == 0 ) &&
        ( CountBits( LoRaMacParams.ChannelsMask[1], 16 ) == 0 ) &&
        ( CountBits( LoRaMacParams.ChannelsMask[2], 16 ) == 0 ) &&
        ( CountBits( LoRaMacParams.ChannelsMask[3], 16 ) == 0 ) &&
        ( CountBits( LoRaMacParams.ChannelsMask[4], 16 ) == 0 ) &&
        ( CountBits( LoRaMacParams.ChannelsMask[5], 16 ) == 0 ) )
    {
        memcpy1( ( uint8_t* )LoRaMacParams.ChannelsMask, ( uint8_t* )LoRaMacParamsDefaults.ChannelsMask, sizeof( LoRaMacParams.ChannelsMask ) );
    }
#else
    if( CountBits( LoRaMacParams.ChannelsMask[0], 16 ) == 0 )
    {
        // Re-enable default channels, if no channel is enabled
        LoRaMacParams.ChannelsMask[0] = LoRaMacParams.ChannelsMask[0] | ( LC( 1 ) + LC( 2 ) + LC( 3 ) );
    }
#endif

    // Update Aggregated duty cycle
    if( AggregatedTimeOff <= TimerGetElapsedTime( AggregatedLastTxDoneTime ) )
    {
        AggregatedTimeOff = 0;

        // Update bands Time OFF
        for( uint8_t i = 0; i < LORA_MAX_NB_BANDS; i++ )
        {
            if( ( IsLoRaMacNetworkJoined == false ) || ( DutyCycleOn == true ) )
            {
                if( Bands[i].TimeOff <= TimerGetElapsedTime( Bands[i].LastTxDoneTime ) )
                {
                    Bands[i].TimeOff = 0;
                }
                if( Bands[i].TimeOff != 0 )
                {
                    nextTxDelay = MIN( Bands[i].TimeOff - TimerGetElapsedTime( Bands[i].LastTxDoneTime ), nextTxDelay );
                }
            }
            else
            {
                if( DutyCycleOn == false )
                {
                    Bands[i].TimeOff = 0;
                }
            }
        }

        // Search how many channels are enabled
        for( uint8_t i = 0, k = 0; i < LORA_MAX_NB_CHANNELS; i += 16, k++ )
        {
            for( uint8_t j = 0; j < 16; j++ )
            {
#if defined( USE_BAND_915 ) || defined( USE_BAND_915_HYBRID )
                if( ( ChannelsMaskRemaining[k] & ( 1 << j ) ) != 0 )
#else
                if( ( LoRaMacParams.ChannelsMask[k] & ( 1 << j ) ) != 0 )
#endif
                {
                    if( Channels[i + j].Frequency == 0 )
                    { // Check if the channel is enabled
                        continue;
                    }
#if defined( USE_BAND_868 ) || defined( USE_BAND_433 ) || defined( USE_BAND_780 )
                    if( IsLoRaMacNetworkJoined == false )
                    {
                        if( ( JOIN_CHANNELS & ( 1 << j ) ) == 0 )
                        {
                            continue;
                        }
                    }
#endif
                    if( ( ( Channels[i + j].DrRange.Fields.Min <= LoRaMacParams.ChannelsDatarate ) &&
                          ( LoRaMacParams.ChannelsDatarate <= Channels[i + j].DrRange.Fields.Max ) ) == false )
                    { // Check if the current channel selection supports the given datarate
                        continue;
                    }
                    if( Bands[Channels[i + j].Band].TimeOff > 0 )
                    { // Check if the band is available for transmission
                        delayTx++;
                        continue;
                    }
                    enabledChannels[nbEnabledChannels++] = i + j;
                }
            }
        }
    }
    else
    {
        delayTx++;
        nextTxDelay = AggregatedTimeOff - TimerGetElapsedTime( AggregatedLastTxDoneTime );
    }

    if( nbEnabledChannels > 0 )
    {
        Channel = enabledChannels[randr( 0, nbEnabledChannels - 1 )];
#if defined( USE_BAND_915 ) || defined( USE_BAND_915_HYBRID )
        if( Channel < ( LORA_MAX_NB_CHANNELS - 8 ) )
        {
            DisableChannelInMask( Channel, ChannelsMaskRemaining );
        }
#endif
        *time = 0;
        return true;
    }
    else
    {
        if( delayTx > 0 )
        {
            // Delay transmission due to AggregatedTimeOff or to a band time off
            *time = nextTxDelay;
            return true;
        }
        // Datarate not supported by any channel
        *time = 0;
        return false;
    }
}

static bool RxWindowSetup( uint32_t freq, int8_t datarate, uint32_t bandwidth, uint16_t timeout, bool rxContinuous )
{
    uint8_t downlinkDatarate = Datarates[datarate];
    RadioModems_t modem;

    if( Radio.GetStatus( ) == RF_IDLE )
    {
        Radio.SetChannel( freq );

        // Store downlink datarate
        McpsIndication.RxDatarate = ( uint8_t ) datarate;

#if defined( USE_BAND_433 ) || defined( USE_BAND_780 ) || defined( USE_BAND_868 )
        if( datarate == DR_7 )
        {
            modem = MODEM_FSK;
            Radio.SetRxConfig( modem, 50e3, downlinkDatarate * 1e3, 0, 83.333e3, 5, 0, false, 0, true, 0, 0, false, rxContinuous );
        }
        else
        {
            modem = MODEM_LORA;
            Radio.SetRxConfig( modem, bandwidth, downlinkDatarate, 1, 0, 8, timeout, false, 0, false, 0, 0, true, rxContinuous );
        }
#elif defined( USE_BAND_470 ) || defined( USE_BAND_915 ) || defined( USE_BAND_915_HYBRID )
        modem = MODEM_LORA;
        Radio.SetRxConfig( modem, bandwidth, downlinkDatarate, 1, 0, 8, timeout, false, 0, false, 0, 0, true, rxContinuous );
#endif

        if( RepeaterSupport == true )
        {
            Radio.SetMaxPayloadLength( modem, MaxPayloadOfDatarateRepeater[datarate] + LORA_MAC_FRMPAYLOAD_OVERHEAD );
        }
        else
        {
            Radio.SetMaxPayloadLength( modem, MaxPayloadOfDatarate[datarate] + LORA_MAC_FRMPAYLOAD_OVERHEAD );
        }

        if( rxContinuous == false )
        {
            Radio.Rx( LoRaMacParams.MaxRxWindow );
        }
        else
        {
            Radio.Rx( 0 ); // Continuous mode
        }
        return true;
    }
    return false;
}

static bool Rx2FreqInRange( uint32_t freq )
{
#if defined( USE_BAND_433 ) || defined( USE_BAND_780 ) || defined( USE_BAND_868 )
    if( Radio.CheckRfFrequency( freq ) == true )
#elif defined( USE_BAND_470 ) || defined( USE_BAND_915 ) || defined( USE_BAND_915_HYBRID )
    if( ( Radio.CheckRfFrequency( freq ) == true ) &&
        ( freq >= LORAMAC_FIRST_RX1_CHANNEL ) &&
        ( freq <= LORAMAC_LAST_RX1_CHANNEL ) &&
        ( ( ( freq - ( uint32_t ) LORAMAC_FIRST_RX1_CHANNEL ) % ( uint32_t ) LORAMAC_STEPWIDTH_RX1_CHANNEL ) == 0 ) )
#endif
    {
        return true;
    }
    return false;
}

static bool ValidatePayloadLength( uint8_t lenN, int8_t datarate, uint8_t fOptsLen )
{
    uint16_t maxN = 0;
    uint16_t payloadSize = 0;

    // Get the maximum payload length
    if( RepeaterSupport == true )
    {
        maxN = MaxPayloadOfDatarateRepeater[datarate];
    }
    else
    {
        maxN = MaxPayloadOfDatarate[datarate];
    }

    // Calculate the resulting payload size
    payloadSize = ( lenN + fOptsLen );

    // Validation of the application payload size
    if( ( payloadSize <= maxN ) && ( payloadSize <= LORAMAC_PHY_MAXPAYLOAD ) )
    {
        return true;
    }
    return false;
}

static uint8_t CountBits( uint16_t mask, uint8_t nbBits )
{
    uint8_t nbActiveBits = 0;

    for( uint8_t j = 0; j < nbBits; j++ )
    {
        if( ( mask & ( 1 << j ) ) == ( 1 << j ) )
        {
            nbActiveBits++;
        }
    }
    return nbActiveBits;
}

#if defined( USE_BAND_915 ) || defined( USE_BAND_915_HYBRID )
static uint8_t CountNbEnabled125kHzChannels( uint16_t *channelsMask )
{
    uint8_t nb125kHzChannels = 0;

    for( uint8_t i = 0, k = 0; i < LORA_MAX_NB_CHANNELS - 8; i += 16, k++ )
    {
        nb125kHzChannels += CountBits( channelsMask[k], 16 );
    }

    return nb125kHzChannels;
}

#if defined( USE_BAND_915_HYBRID )
static void ReenableChannels( uint16_t mask, uint16_t* channelsMask )
{
    uint16_t blockMask = mask;

    for( uint8_t i = 0, j = 0; i < 4; i++, j += 2 )
    {
        channelsMask[i] = 0;
        if( ( blockMask & ( 1 << j ) ) != 0 )
        {
            channelsMask[i] |= 0x00FF;
        }
        if( ( blockMask & ( 1 << ( j + 1 ) ) ) != 0 )
        {
            channelsMask[i] |= 0xFF00;
        }
    }
    channelsMask[4] = blockMask;
    channelsMask[5] = 0x0000;
}

static bool ValidateChannelMask( uint16_t* channelsMask )
{
    bool chanMaskState = false;
    uint16_t block1 = 0;
    uint16_t block2 = 0;
    uint8_t index = 0;

    for( uint8_t i = 0; i < 4; i++ )
    {
        block1 = channelsMask[i] & 0x00FF;
        block2 = channelsMask[i] & 0xFF00;

        if( ( CountBits( block1, 16 ) > 5 ) && ( chanMaskState == false ) )
        {
            channelsMask[i] &= block1;
            channelsMask[4] = 1 << ( i * 2 );
            chanMaskState = true;
            index = i;
        }
        else if( ( CountBits( block2, 16 ) > 5 ) && ( chanMaskState == false ) )
        {
            channelsMask[i] &= block2;
            channelsMask[4] = 1 << ( i * 2 + 1 );
            chanMaskState = true;
            index = i;
        }
    }

    // Do only change the channel mask, if we have found a valid block.
    if( chanMaskState == true )
    {
        for( uint8_t i = 0; i < 4; i++ )
        {
            if( i != index )
            {
                channelsMask[i] = 0;
            }
        }
    }
    return chanMaskState;
}
#endif
#endif

static bool ValidateDatarate( int8_t datarate, uint16_t* channelsMask )
{
    if( ValueInRange( datarate, LORAMAC_TX_MIN_DATARATE, LORAMAC_TX_MAX_DATARATE ) == false )
    {
        return false;
    }
    for( uint8_t i = 0, k = 0; i < LORA_MAX_NB_CHANNELS; i += 16, k++ )
    {
        for( uint8_t j = 0; j < 16; j++ )
        {
            if( ( ( channelsMask[k] & ( 1 << j ) ) != 0 ) )
            {// Check datarate validity for enabled channels
                if( ValueInRange( datarate, Channels[i + j].DrRange.Fields.Min, Channels[i + j].DrRange.Fields.Max ) == true )
                {
                    // At least 1 channel has been found we can return OK.
                    return true;
                }
            }
        }
    }
    return false;
}

static int8_t LimitTxPower( int8_t txPower, int8_t maxBandTxPower )
{
    int8_t resultTxPower = txPower;

    // Limit tx power to the band max
    resultTxPower =  MAX( txPower, maxBandTxPower );

#if defined( USE_BAND_915 ) || defined( USE_BAND_915_HYBRID )
    if( ( LoRaMacParams.ChannelsDatarate == DR_4 ) ||
        ( ( LoRaMacParams.ChannelsDatarate >= DR_8 ) && ( LoRaMacParams.ChannelsDatarate <= DR_13 ) ) )
    {// Limit tx power to max 26dBm
        resultTxPower =  MAX( txPower, TX_POWER_26_DBM );
    }
    else
    {
        if( CountNbEnabled125kHzChannels( LoRaMacParams.ChannelsMask ) < 50 )
        {// Limit tx power to max 21dBm
            resultTxPower = MAX( txPower, TX_POWER_20_DBM );
        }
    }
#endif
    return resultTxPower;
}

static bool ValueInRange( int8_t value, int8_t min, int8_t max )
{
    if( ( value >= min ) && ( value <= max ) )
    {
        return true;
    }
    return false;
}

static bool DisableChannelInMask( uint8_t id, uint16_t* mask )
{
    uint8_t index = 0;
    index = id / 16;

    if( ( index > 4 ) || ( id >= LORA_MAX_NB_CHANNELS ) )
    {
        return false;
    }

    // Deactivate channel
    mask[index] &= ~( 1 << ( id % 16 ) );

    return true;
}

static bool AdrNextDr( bool adrEnabled, bool updateChannelMask, int8_t* datarateOut )
{
    bool adrAckReq = false;
    int8_t datarate = LoRaMacParams.ChannelsDatarate;

    if( adrEnabled == true )
    {
        if( datarate == LORAMAC_TX_MIN_DATARATE )
        {
            AdrAckCounter = 0;
            adrAckReq = false;
        }
        else
        {
            if( AdrAckCounter >= ADR_ACK_LIMIT )
            {
                adrAckReq = true;
                LoRaMacParams.ChannelsTxPower = LORAMAC_MAX_TX_POWER;
            }
            else
            {
                adrAckReq = false;
            }
            if( AdrAckCounter >= ( ADR_ACK_LIMIT + ADR_ACK_DELAY ) )
            {
                if( ( AdrAckCounter % ADR_ACK_DELAY ) == 0 )
                {
#if defined( USE_BAND_433 ) || defined( USE_BAND_780 ) || defined( USE_BAND_868 )
                    if( datarate > LORAMAC_TX_MIN_DATARATE )
                    {
                        datarate--;
                    }
                    if( datarate == LORAMAC_TX_MIN_DATARATE )
                    {
                        if( updateChannelMask == true )
                        {
                            // Re-enable default channels LC1, LC2, LC3
                            LoRaMacParams.ChannelsMask[0] = LoRaMacParams.ChannelsMask[0] | ( LC( 1 ) + LC( 2 ) + LC( 3 ) );
                        }
                    }
#elif defined( USE_BAND_470 )
                    if( datarate > LORAMAC_TX_MIN_DATARATE )
                    {
                        datarate--;
                    }
                    if( datarate == LORAMAC_TX_MIN_DATARATE )
                    {
                        if( updateChannelMask == true )
                        {
                            // Re-enable default channels
                            memcpy1( ( uint8_t* )LoRaMacParams.ChannelsMask, ( uint8_t* )LoRaMacParamsDefaults.ChannelsMask, sizeof( LoRaMacParams.ChannelsMask ) );
                        }
                    }
#elif defined( USE_BAND_915 ) || defined( USE_BAND_915_HYBRID )
                    if( ( datarate > LORAMAC_TX_MIN_DATARATE ) && ( datarate == DR_8 ) )
                    {
                        datarate = DR_4;
                    }
                    else if( datarate > LORAMAC_TX_MIN_DATARATE )
                    {
                        datarate--;
                    }
                    if( datarate == LORAMAC_TX_MIN_DATARATE )
                    {
                        if( updateChannelMask == true )
                        {
#if defined( USE_BAND_915 )
                            // Re-enable default channels
                            memcpy1( ( uint8_t* )LoRaMacParams.ChannelsMask, ( uint8_t* )LoRaMacParamsDefaults.ChannelsMask, sizeof( LoRaMacParams.ChannelsMask ) );
#else // defined( USE_BAND_915_HYBRID )
                            // Re-enable default channels
                            ReenableChannels( LoRaMacParamsDefaults.ChannelsMask[4], LoRaMacParams.ChannelsMask );
#endif
                        }
                    }
#else
#error "Please define a frequency band in the compiler options."
#endif
                }
            }
        }
    }

    *datarateOut = datarate;

    return adrAckReq;
}

static LoRaMacStatus_t AddMacCommand( uint8_t cmd, uint8_t p1, uint8_t p2 )
{
    LoRaMacStatus_t status = LORAMAC_STATUS_BUSY;
    // The maximum buffer length must take MAC commands to re-send into account.
    uint8_t bufLen = LORA_MAC_COMMAND_MAX_LENGTH - MacCommandsBufferToRepeatIndex;

    switch( cmd )
    {
        case MOTE_MAC_LINK_CHECK_REQ:
            if( MacCommandsBufferIndex < bufLen )
            {
                MacCommandsBuffer[MacCommandsBufferIndex++] = cmd;
                // No payload for this command
                status = LORAMAC_STATUS_OK;
            }
            break;
        case MOTE_MAC_LINK_ADR_ANS:
            if( MacCommandsBufferIndex < ( bufLen - 1 ) )
            {
                MacCommandsBuffer[MacCommandsBufferIndex++] = cmd;
                // Margin
                MacCommandsBuffer[MacCommandsBufferIndex++] = p1;
                status = LORAMAC_STATUS_OK;
            }
            break;
        case MOTE_MAC_DUTY_CYCLE_ANS:
            if( MacCommandsBufferIndex < bufLen )
            {
                MacCommandsBuffer[MacCommandsBufferIndex++] = cmd;
                // No payload for this answer
                status = LORAMAC_STATUS_OK;
            }
            break;
        case MOTE_MAC_RX_PARAM_SETUP_ANS:
            if( MacCommandsBufferIndex < ( bufLen - 1 ) )
            {
                MacCommandsBuffer[MacCommandsBufferIndex++] = cmd;
                // Status: Datarate ACK, Channel ACK
                MacCommandsBuffer[MacCommandsBufferIndex++] = p1;
                status = LORAMAC_STATUS_OK;
            }
            break;
        case MOTE_MAC_DEV_STATUS_ANS:
            if( MacCommandsBufferIndex < ( bufLen - 2 ) )
            {
                MacCommandsBuffer[MacCommandsBufferIndex++] = cmd;
                // 1st byte Battery
                // 2nd byte Margin
                MacCommandsBuffer[MacCommandsBufferIndex++] = p1;
                MacCommandsBuffer[MacCommandsBufferIndex++] = p2;
                status = LORAMAC_STATUS_OK;
            }
            break;
        case MOTE_MAC_NEW_CHANNEL_ANS:
            if( MacCommandsBufferIndex < ( bufLen - 1 ) )
            {
                MacCommandsBuffer[MacCommandsBufferIndex++] = cmd;
                // Status: Datarate range OK, Channel frequency OK
                MacCommandsBuffer[MacCommandsBufferIndex++] = p1;
                status = LORAMAC_STATUS_OK;
            }
            break;
        case MOTE_MAC_RX_TIMING_SETUP_ANS:
            if( MacCommandsBufferIndex < bufLen )
            {
                MacCommandsBuffer[MacCommandsBufferIndex++] = cmd;
                // No payload for this answer
                status = LORAMAC_STATUS_OK;
            }
            break;
        default:
            return LORAMAC_STATUS_SERVICE_UNKNOWN;
    }
    if( status == LORAMAC_STATUS_OK )
    {
        MacCommandsInNextTx = true;
    }
    return status;
}

static uint8_t ParseMacCommandsToRepeat( uint8_t* cmdBufIn, uint8_t length, uint8_t* cmdBufOut )
{
    uint8_t i = 0;
    uint8_t cmdCount = 0;

    if( ( cmdBufIn == NULL ) || ( cmdBufOut == NULL ) )
    {
        return 0;
    }

    for( i = 0; i < length; i++ )
    {
        switch( cmdBufIn[i] )
        {
            // STICKY
            case MOTE_MAC_RX_PARAM_SETUP_ANS:
            {
                cmdBufOut[cmdCount++] = cmdBufIn[i++];
                cmdBufOut[cmdCount++] = cmdBufIn[i];
                break;
            }
            case MOTE_MAC_RX_TIMING_SETUP_ANS:
            {
                cmdBufOut[cmdCount++] = cmdBufIn[i];
                break;
            }
            // NON-STICKY
            case MOTE_MAC_DEV_STATUS_ANS:
            { // 2 bytes payload
                i += 2;
                break;
            }
            case MOTE_MAC_LINK_ADR_ANS:
            case MOTE_MAC_NEW_CHANNEL_ANS:
            { // 1 byte payload
                i++;
                break;
            }
            case MOTE_MAC_DUTY_CYCLE_ANS:
            case MOTE_MAC_LINK_CHECK_REQ:
            { // 0 byte payload
                break;
            }
            default:
                break;
        }
    }

    return cmdCount;
}

static void ProcessMacCommands( uint8_t *payload, uint8_t macIndex, uint8_t commandsSize, uint8_t snr )
{
    uint8_t status = 0;

    while( macIndex < commandsSize )
    {
        // Decode Frame MAC commands
        switch( payload[macIndex++] )
        {
            case SRV_MAC_LINK_CHECK_ANS:
                MlmeConfirm.Status = LORAMAC_EVENT_INFO_STATUS_OK;
                MlmeConfirm.DemodMargin = payload[macIndex++];
                MlmeConfirm.NbGateways = payload[macIndex++];
                break;
            case SRV_MAC_LINK_ADR_REQ:
                {
                    LinkAdrReqParams_t linkAdrReq;
                    int8_t linkAdrDatarate = DR_0;
                    int8_t linkAdrTxPower = TX_POWER_0;
                    uint8_t linkAdrNbRep = 0;
                    uint8_t linkAdrNbBytesParsed = 0;

                    // Fill parameter structure
                    linkAdrReq.Payload = &payload[macIndex - 1];
                    linkAdrReq.PayloadSize = commandsSize - ( macIndex - 1 );

                    // Process the ADR requests
                    status = RegionLinkAdrReq( LoRaMacRegion, &linkAdrReq, &linkAdrDatarate,
                                               &linkAdrTxPower, &linkAdrNbRep, &linkAdrNbBytesParsed );

                    if( ( status & 0x07 ) == 0x07 )
                    {
                        if( ( AdrCtrlOn == false ) &&
                            ( ( LoRaMacParams.ChannelsDatarate != linkAdrDatarate ) ||
                            ( LoRaMacParams.ChannelsTxPower != linkAdrTxPower ) ) )
                        { // ADR disabled don't handle ADR requests if server tries to change datarate or tx power
                            status = 0;
                        }
                        else
                        {
                            LoRaMacParams.ChannelsDatarate = linkAdrDatarate;
                            LoRaMacParams.ChannelsTxPower = linkAdrTxPower;
                            LoRaMacParams.ChannelsNbRep = linkAdrNbRep;
                        }
                    }

                    // Add the answers to the buffer
                    for( uint8_t i = 0; i < ( linkAdrNbBytesParsed / 5 ); i++ )
                    {
                        AddMacCommand( MOTE_MAC_LINK_ADR_ANS, status, 0 );
                    }
                    // Update MAC index
                    macIndex += linkAdrNbBytesParsed - 1;
                }
                break;
            case SRV_MAC_DUTY_CYCLE_REQ:
                MaxDCycle = payload[macIndex++];
                AggregatedDCycle = 1 << MaxDCycle;
                AddMacCommand( MOTE_MAC_DUTY_CYCLE_ANS, 0, 0 );
                break;
            case SRV_MAC_RX_PARAM_SETUP_REQ:
                {
                    RxParamSetupReqParams_t rxParamSetupReq;
                    status = 0x07;

                    rxParamSetupReq.DrOffset = ( payload[macIndex] >> 4 ) & 0x07;
                    rxParamSetupReq.Datarate = payload[macIndex] & 0x0F;
                    macIndex++;

                    rxParamSetupReq.Frequency =  ( uint32_t )payload[macIndex++];
                    rxParamSetupReq.Frequency |= ( uint32_t )payload[macIndex++] << 8;
                    rxParamSetupReq.Frequency |= ( uint32_t )payload[macIndex++] << 16;
                    rxParamSetupReq.Frequency *= 100;

                    // Perform request on region
                    status = RegionRxParamSetupReq( LoRaMacRegion, &rxParamSetupReq );

                    if( ( status & 0x07 ) == 0x07 )
                    {
                        LoRaMacParams.Rx2Channel.Datarate = rxParamSetupReq.Datarate;
                        LoRaMacParams.Rx2Channel.Frequency = rxParamSetupReq.Frequency;
                        LoRaMacParams.Rx1DrOffset = rxParamSetupReq.DrOffset;
                    }
                    AddMacCommand( MOTE_MAC_RX_PARAM_SETUP_ANS, status, 0 );
                }
                break;
            case SRV_MAC_DEV_STATUS_REQ:
                {
                    uint8_t batteryLevel = BAT_LEVEL_NO_MEASURE;
                    if( ( LoRaMacCallbacks != NULL ) && ( LoRaMacCallbacks->GetBatteryLevel != NULL ) )
                    {
                        batteryLevel = LoRaMacCallbacks->GetBatteryLevel( );
                    }
                    AddMacCommand( MOTE_MAC_DEV_STATUS_ANS, batteryLevel, snr );
                    break;
                }
            case SRV_MAC_NEW_CHANNEL_REQ:
                {
                    NewChannelReqParams_t newChannelReq;
                    ChannelParams_t chParam;
                    status = 0x03;

                    newChannelReq.ChannelId = payload[macIndex++];
                    newChannelReq.NewChannel = &chParam;

                    chParam.Frequency = ( uint32_t )payload[macIndex++];
                    chParam.Frequency |= ( uint32_t )payload[macIndex++] << 8;
                    chParam.Frequency |= ( uint32_t )payload[macIndex++] << 16;
                    chParam.Frequency *= 100;
                    chParam.Rx1Frequency = 0;
                    chParam.DrRange.Value = payload[macIndex++];

                    status = RegionNewChannelReq( LoRaMacRegion, &newChannelReq );

                    AddMacCommand( MOTE_MAC_NEW_CHANNEL_ANS, status, 0 );
                }
                break;
            case SRV_MAC_RX_TIMING_SETUP_REQ:
                {
                    uint8_t delay = payload[macIndex++] & 0x0F;

                    if( delay == 0 )
                    {
                        delay++;
                    }
                    LoRaMacParams.ReceiveDelay1 = delay * 1e3;
                    LoRaMacParams.ReceiveDelay2 = LoRaMacParams.ReceiveDelay1 + 1e3;
                    AddMacCommand( MOTE_MAC_RX_TIMING_SETUP_ANS, 0, 0 );
                }
                break;
            case SRV_MAC_TX_PARAM_SETUP_REQ:
                {
                    TxParamSetupReqParams_t txParamSetupReq;
                    uint8_t eirpDwellTime = payload[macIndex++];

                    txParamSetupReq.UplinkDwellTime = 0;
                    txParamSetupReq.DownlinkDwellTime = 0;

                    if( ( eirpDwellTime & 0x20 ) == 0x20 )
                    {
                        txParamSetupReq.UplinkDwellTime = 1;
                    }
                    if( ( eirpDwellTime & 0x10 ) == 0x10 )
                    {
                        txParamSetupReq.DownlinkDwellTime = 1;
                    }
                    txParamSetupReq.MaxEirp = eirpDwellTime & 0x0F;

                    // Check the status for correctness
                    if( RegionTxParamSetupReq( LoRaMacRegion, &txParamSetupReq ) != -1 )
                    {
                        // Accept command
                        LoRaMacParams.UplinkDwellTime = txParamSetupReq.UplinkDwellTime;
                        LoRaMacParams.DownlinkDwellTime = txParamSetupReq.DownlinkDwellTime;
                        LoRaMacParams.MaxEirp = LoRaMacMaxEirpTable[txParamSetupReq.MaxEirp];
                        // Add command response
                        AddMacCommand( MOTE_MAC_TX_PARAM_SETUP_ANS, 0, 0 );
                    }
                }
                break;
            case SRV_MAC_DL_CHANNEL_REQ:
                {
                    DlChannelReqParams_t dlChannelReq;
                    status = 0x03;

                    dlChannelReq.ChannelId = payload[macIndex++];
                    dlChannelReq.Rx1Frequency = ( uint32_t )payload[macIndex++];
                    dlChannelReq.Rx1Frequency |= ( uint32_t )payload[macIndex++] << 8;
                    dlChannelReq.Rx1Frequency |= ( uint32_t )payload[macIndex++] << 16;
                    dlChannelReq.Rx1Frequency *= 100;

                    status = RegionDlChannelReq( LoRaMacRegion, &dlChannelReq );

                    AddMacCommand( MOTE_MAC_DL_CHANNEL_ANS, status, 0 );
                }
                break;
            default:
                // Unknown command. ABORT MAC commands processing
                return;
        }
    }
}

LoRaMacStatus_t Send( LoRaMacHeader_t *macHdr, uint8_t fPort, void *fBuffer, uint16_t fBufferSize )
{
    LoRaMacFrameCtrl_t fCtrl;
    LoRaMacStatus_t status = LORAMAC_STATUS_PARAMETER_INVALID;

    fCtrl.Value = 0;
    fCtrl.Bits.FOptsLen      = 0;
    fCtrl.Bits.FPending      = 0;
    fCtrl.Bits.Ack           = false;
    fCtrl.Bits.AdrAckReq     = false;
    fCtrl.Bits.Adr           = AdrCtrlOn;

    // Prepare the frame
    status = PrepareFrame( macHdr, &fCtrl, fPort, fBuffer, fBufferSize );

    // Validate status
    if( status != LORAMAC_STATUS_OK )
    {
        return status;
    }

    // Reset confirm parameters
    McpsConfirm.NbRetries = 0;
    McpsConfirm.AckReceived = false;
    McpsConfirm.UpLinkCounter = UpLinkCounter;

    status = ScheduleTx( );

    return status;
}

static LoRaMacStatus_t ScheduleTx( )
{
    TimerTime_t dutyCycleTimeOff = 0;
    NextChanParams_t nextChan;

    // Check if the device is off
    if( MaxDCycle == 255 )
    {
        return LORAMAC_STATUS_DEVICE_OFF;
    }
    if( MaxDCycle == 0 )
    {
        AggregatedTimeOff = 0;
    }

    nextChan.AggrTimeOff = AggregatedTimeOff;
    nextChan.Datarate = LoRaMacParams.ChannelsDatarate;
    nextChan.DutyCycleEnabled = DutyCycleOn;
    nextChan.Joined = IsLoRaMacNetworkJoined;
    nextChan.LastAggrTx = AggregatedLastTxDoneTime;

    // Select channel
    while( RegionNextChannel( LoRaMacRegion, &nextChan, &Channel, &dutyCycleTimeOff ) == false )
    {
        // Set the default datarate
        LoRaMacParams.ChannelsDatarate = LoRaMacParamsDefaults.ChannelsDatarate;
        // Update datarate in the function parameters
        nextChan.Datarate = LoRaMacParams.ChannelsDatarate;
    }

    // Schedule transmission of frame
    if( dutyCycleTimeOff == 0 )
    {
        // Try to send now
        return SendFrameOnChannel( Channel );
    }
    else
    {
        // Send later - prepare timer
        LoRaMacState |= LORAMAC_TX_DELAYED;
        TimerSetValue( &TxDelayedTimer, dutyCycleTimeOff );
        TimerStart( &TxDelayedTimer );

        return LORAMAC_STATUS_OK;
    }
}

static void CalculateBackOff( uint8_t channel )
{
    CalcBackOffParams_t calcBackOff;

    calcBackOff.Joined = IsLoRaMacNetworkJoined;
    calcBackOff.DutyCycleEnabled = DutyCycleOn;
    calcBackOff.Channel = channel;
    calcBackOff.ElapsedTime = TimerGetElapsedTime( LoRaMacInitializationTime );
    calcBackOff.TxTimeOnAir = TxTimeOnAir;

    // Update regional back-off
    RegionCalcBackOff( LoRaMacRegion, &calcBackOff );

    // Update aggregated time-off
    AggregatedTimeOff = AggregatedTimeOff + ( TxTimeOnAir * AggregatedDCycle - TxTimeOnAir );
}

static void ResetMacParameters( void )
{
    IsLoRaMacNetworkJoined = false;

    // Counters
    UpLinkCounter = 0;
    DownLinkCounter = 0;
    AdrAckCounter = 0;

    ChannelsNbRepCounter = 0;

    AckTimeoutRetries = 1;
    AckTimeoutRetriesCounter = 1;
    AckTimeoutRetry = false;

    MaxDCycle = 0;
    AggregatedDCycle = 1;

    MacCommandsBufferIndex = 0;
    MacCommandsBufferToRepeatIndex = 0;

    IsRxWindowsEnabled = true;

    LoRaMacParams.ChannelsTxPower = LoRaMacParamsDefaults.ChannelsTxPower;
    LoRaMacParams.ChannelsDatarate = LoRaMacParamsDefaults.ChannelsDatarate;
    LoRaMacParams.MaxRxWindow = LoRaMacParamsDefaults.MaxRxWindow;
    LoRaMacParams.ReceiveDelay1 = LoRaMacParamsDefaults.ReceiveDelay1;
    LoRaMacParams.ReceiveDelay2 = LoRaMacParamsDefaults.ReceiveDelay2;
    LoRaMacParams.JoinAcceptDelay1 = LoRaMacParamsDefaults.JoinAcceptDelay1;
    LoRaMacParams.JoinAcceptDelay2 = LoRaMacParamsDefaults.JoinAcceptDelay2;
    LoRaMacParams.Rx1DrOffset = LoRaMacParamsDefaults.Rx1DrOffset;
    LoRaMacParams.ChannelsNbRep = LoRaMacParamsDefaults.ChannelsNbRep;
    LoRaMacParams.Rx2Channel = LoRaMacParamsDefaults.Rx2Channel;
    LoRaMacParams.UplinkDwellTime = LoRaMacParamsDefaults.UplinkDwellTime;
    LoRaMacParams.DownlinkDwellTime = LoRaMacParamsDefaults.DownlinkDwellTime;
    LoRaMacParams.MaxEirp = LoRaMacParamsDefaults.MaxEirp;

    NodeAckRequested = false;
    SrvAckRequested = false;
    MacCommandsInNextTx = false;

    // Reset Multicast downlink counters
    MulticastParams_t *cur = MulticastChannels;
    while( cur != NULL )
    {
        cur->DownLinkCounter = 0;
        cur = cur->Next;
    }

    // Initialize channel index.
    // ToDo Check the initialization value
    Channel = 0;

    // Store the current initialization time
    LoRaMacInitializationTime = TimerGetCurrentTime( );
}

LoRaMacStatus_t PrepareFrame( LoRaMacHeader_t *macHdr, LoRaMacFrameCtrl_t *fCtrl, uint8_t fPort, void *fBuffer, uint16_t fBufferSize )
{
    AdrNextParams_t adrNext;
    uint16_t i;
    uint8_t pktHeaderLen = 0;
    uint32_t mic = 0;
    const void* payload = fBuffer;
    uint8_t framePort = fPort;

    LoRaMacBufferPktLen = 0;

    NodeAckRequested = false;

    if( fBuffer == NULL )
    {
        fBufferSize = 0;
    }

    LoRaMacTxPayloadLen = fBufferSize;

    LoRaMacBuffer[pktHeaderLen++] = macHdr->Value;

    switch( macHdr->Bits.MType )
    {
        case FRAME_TYPE_JOIN_REQ:
            RxWindow1Delay = LoRaMacParams.JoinAcceptDelay1 - RADIO_WAKEUP_TIME;
            RxWindow2Delay = LoRaMacParams.JoinAcceptDelay2 - RADIO_WAKEUP_TIME;

            LoRaMacBufferPktLen = pktHeaderLen;

            memcpyr( LoRaMacBuffer + LoRaMacBufferPktLen, LoRaMacAppEui, 8 );
            LoRaMacBufferPktLen += 8;
            memcpyr( LoRaMacBuffer + LoRaMacBufferPktLen, LoRaMacDevEui, 8 );
            LoRaMacBufferPktLen += 8;

            LoRaMacDevNonce = Radio.Random( );

            LoRaMacBuffer[LoRaMacBufferPktLen++] = LoRaMacDevNonce & 0xFF;
            LoRaMacBuffer[LoRaMacBufferPktLen++] = ( LoRaMacDevNonce >> 8 ) & 0xFF;

            LoRaMacJoinComputeMic( LoRaMacBuffer, LoRaMacBufferPktLen & 0xFF, LoRaMacAppKey, &mic );

            LoRaMacBuffer[LoRaMacBufferPktLen++] = mic & 0xFF;
            LoRaMacBuffer[LoRaMacBufferPktLen++] = ( mic >> 8 ) & 0xFF;
            LoRaMacBuffer[LoRaMacBufferPktLen++] = ( mic >> 16 ) & 0xFF;
            LoRaMacBuffer[LoRaMacBufferPktLen++] = ( mic >> 24 ) & 0xFF;

            break;
        case FRAME_TYPE_DATA_CONFIRMED_UP:
            NodeAckRequested = true;
            //Intentional fallthrough
        case FRAME_TYPE_DATA_UNCONFIRMED_UP:
            if( IsLoRaMacNetworkJoined == false )
            {
                return LORAMAC_STATUS_NO_NETWORK_JOINED; // No network has been joined yet
            }

            // Adr next request
            adrNext.UpdateChanMask = true;
            adrNext.AdrEnabled = fCtrl->Bits.Adr;
            adrNext.AdrAckCounter = AdrAckCounter;
            adrNext.Datarate = LoRaMacParams.ChannelsDatarate;
            adrNext.TxPower = LoRaMacParams.ChannelsTxPower;

            fCtrl->Bits.AdrAckReq = RegionAdrNext( LoRaMacRegion, &adrNext,
                                                   &LoRaMacParams.ChannelsDatarate, &LoRaMacParams.ChannelsTxPower, &AdrAckCounter );

            if( ValidatePayloadLength( LoRaMacTxPayloadLen, LoRaMacParams.ChannelsDatarate, MacCommandsBufferIndex ) == false )
            {
                return LORAMAC_STATUS_LENGTH_ERROR;
            }

            RxWindow1Delay = LoRaMacParams.ReceiveDelay1 - RADIO_WAKEUP_TIME;
            RxWindow2Delay = LoRaMacParams.ReceiveDelay2 - RADIO_WAKEUP_TIME;

            if( SrvAckRequested == true )
            {
                SrvAckRequested = false;
                fCtrl->Bits.Ack = 1;
            }

            LoRaMacBuffer[pktHeaderLen++] = ( LoRaMacDevAddr ) & 0xFF;
            LoRaMacBuffer[pktHeaderLen++] = ( LoRaMacDevAddr >> 8 ) & 0xFF;
            LoRaMacBuffer[pktHeaderLen++] = ( LoRaMacDevAddr >> 16 ) & 0xFF;
            LoRaMacBuffer[pktHeaderLen++] = ( LoRaMacDevAddr >> 24 ) & 0xFF;

            LoRaMacBuffer[pktHeaderLen++] = fCtrl->Value;

            LoRaMacBuffer[pktHeaderLen++] = UpLinkCounter & 0xFF;
            LoRaMacBuffer[pktHeaderLen++] = ( UpLinkCounter >> 8 ) & 0xFF;

            // Copy the MAC commands which must be re-send into the MAC command buffer
            memcpy1( &MacCommandsBuffer[MacCommandsBufferIndex], MacCommandsBufferToRepeat, MacCommandsBufferToRepeatIndex );
            MacCommandsBufferIndex += MacCommandsBufferToRepeatIndex;

            if( ( payload != NULL ) && ( LoRaMacTxPayloadLen > 0 ) )
            {
                if( ( MacCommandsBufferIndex <= LORA_MAC_COMMAND_MAX_LENGTH ) && ( MacCommandsInNextTx == true ) )
                {
                    fCtrl->Bits.FOptsLen += MacCommandsBufferIndex;

                    // Update FCtrl field with new value of OptionsLength
                    LoRaMacBuffer[0x05] = fCtrl->Value;
                    for( i = 0; i < MacCommandsBufferIndex; i++ )
                    {
                        LoRaMacBuffer[pktHeaderLen++] = MacCommandsBuffer[i];
                    }
                }
            }
            else
            {
                if( ( MacCommandsBufferIndex > 0 ) && ( MacCommandsInNextTx ) )
                {
                    LoRaMacTxPayloadLen = MacCommandsBufferIndex;
                    payload = MacCommandsBuffer;
                    framePort = 0;
                }
            }
            MacCommandsInNextTx = false;
            // Store MAC commands which must be re-send in case the device does not receive a downlink anymore
            MacCommandsBufferToRepeatIndex = ParseMacCommandsToRepeat( MacCommandsBuffer, MacCommandsBufferIndex, MacCommandsBufferToRepeat );
            if( MacCommandsBufferToRepeatIndex > 0 )
            {
                MacCommandsInNextTx = true;
            }
            MacCommandsBufferIndex = 0;

            if( ( payload != NULL ) && ( LoRaMacTxPayloadLen > 0 ) )
            {
                LoRaMacBuffer[pktHeaderLen++] = framePort;

                if( framePort == 0 )
                {
                    LoRaMacPayloadEncrypt( (uint8_t* ) payload, LoRaMacTxPayloadLen, LoRaMacNwkSKey, LoRaMacDevAddr, UP_LINK, UpLinkCounter, LoRaMacPayload );
                }
                else
                {
                    LoRaMacPayloadEncrypt( (uint8_t* ) payload, LoRaMacTxPayloadLen, LoRaMacAppSKey, LoRaMacDevAddr, UP_LINK, UpLinkCounter, LoRaMacPayload );
                }
                memcpy1( LoRaMacBuffer + pktHeaderLen, LoRaMacPayload, LoRaMacTxPayloadLen );
            }
            LoRaMacBufferPktLen = pktHeaderLen + LoRaMacTxPayloadLen;

            LoRaMacComputeMic( LoRaMacBuffer, LoRaMacBufferPktLen, LoRaMacNwkSKey, LoRaMacDevAddr, UP_LINK, UpLinkCounter, &mic );

            LoRaMacBuffer[LoRaMacBufferPktLen + 0] = mic & 0xFF;
            LoRaMacBuffer[LoRaMacBufferPktLen + 1] = ( mic >> 8 ) & 0xFF;
            LoRaMacBuffer[LoRaMacBufferPktLen + 2] = ( mic >> 16 ) & 0xFF;
            LoRaMacBuffer[LoRaMacBufferPktLen + 3] = ( mic >> 24 ) & 0xFF;

            LoRaMacBufferPktLen += LORAMAC_MFR_LEN;

            break;
        case FRAME_TYPE_PROPRIETARY:
            if( ( fBuffer != NULL ) && ( LoRaMacTxPayloadLen > 0 ) )
            {
                memcpy1( LoRaMacBuffer + pktHeaderLen, ( uint8_t* ) fBuffer, LoRaMacTxPayloadLen );
                LoRaMacBufferPktLen = pktHeaderLen + LoRaMacTxPayloadLen;
            }
            break;
        default:
            return LORAMAC_STATUS_SERVICE_UNKNOWN;
    }

    return LORAMAC_STATUS_OK;
}

LoRaMacStatus_t SendFrameOnChannel( uint8_t channel )
{
    TxConfigParams_t txConfig;
    int8_t txPower = 0;

    txConfig.Channel = channel;
    txConfig.Datarate = LoRaMacParams.ChannelsDatarate;
    txConfig.TxPower = LoRaMacParams.ChannelsTxPower;
    txConfig.MaxEirp = LoRaMacParams.MaxEirp;
    txConfig.PktLen = LoRaMacBufferPktLen;

    RegionTxConfig( LoRaMacRegion, &txConfig, &txPower, &TxTimeOnAir );

    MlmeConfirm.Status = LORAMAC_EVENT_INFO_STATUS_ERROR;
    McpsConfirm.Status = LORAMAC_EVENT_INFO_STATUS_ERROR;
    McpsConfirm.Datarate = LoRaMacParams.ChannelsDatarate;
    McpsConfirm.TxPower = txPower;

    // Store the time on air
    McpsConfirm.TxTimeOnAir = TxTimeOnAir;
    MlmeConfirm.TxTimeOnAir = TxTimeOnAir;

    // Starts the MAC layer status check timer
    TimerSetValue( &MacStateCheckTimer, MAC_STATE_CHECK_TIMEOUT );
    TimerStart( &MacStateCheckTimer );

    if( IsLoRaMacNetworkJoined == false )
    {
        JoinRequestTrials++;
    }

    // Send now
    Radio.Send( LoRaMacBuffer, LoRaMacBufferPktLen );

    LoRaMacState |= LORAMAC_TX_RUNNING;

    return LORAMAC_STATUS_OK;
}

LoRaMacStatus_t SetTxContinuousWave( uint16_t timeout )
{
    ContinuousWaveParams_t continuousWave;

    continuousWave.Channel = Channel;
    continuousWave.Datarate = LoRaMacParams.ChannelsDatarate;
    continuousWave.TxPower = LoRaMacParams.ChannelsTxPower;
    continuousWave.MaxEirp = LoRaMacParams.MaxEirp;
    continuousWave.Timeout = timeout;

    RegionSetContinuousWave( LoRaMacRegion, &continuousWave );

    // Starts the MAC layer status check timer
    TimerSetValue( &MacStateCheckTimer, MAC_STATE_CHECK_TIMEOUT );
    TimerStart( &MacStateCheckTimer );

    LoRaMacState |= LORAMAC_TX_RUNNING;

    return LORAMAC_STATUS_OK;
}

LoRaMacStatus_t LoRaMacInitialization( LoRaMacPrimitives_t *primitives, LoRaMacCallback_t *callbacks, LoRaMacRegion_t region )
{
    GetPhyParams_t getPhy;

    if( primitives == NULL )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }

    if( ( primitives->MacMcpsConfirm == NULL ) ||
        ( primitives->MacMcpsIndication == NULL ) ||
        ( primitives->MacMlmeConfirm == NULL ) )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }
    // Verify if the region is supported
    if( RegionIsActive( region ) == false )
    {
        return LORAMAC_STATUS_REGION_NOT_SUPPORTED;
    }

    LoRaMacPrimitives = primitives;
    LoRaMacCallbacks = callbacks;
    LoRaMacRegion = region;

    LoRaMacFlags.Value = 0;

    LoRaMacDeviceClass = CLASS_A;
    LoRaMacState = LORAMAC_IDLE;

    JoinRequestTrials = 0;
    MaxJoinRequestTrials = 1;
    RepeaterSupport = false;

    // Reset duty cycle times
    AggregatedLastTxDoneTime = 0;
    AggregatedTimeOff = 0;

    // Reset to defaults
    getPhy.Attribute = PHY_DUTY_CYCLE;
    RegionGetPhyParam( LoRaMacRegion, &getPhy );
    DutyCycleOn = ( bool ) getPhy.Param.Value;

    getPhy.Attribute = PHY_DEF_TX_POWER;
    RegionGetPhyParam( LoRaMacRegion, &getPhy );
    LoRaMacParamsDefaults.ChannelsTxPower = getPhy.Param.Value;

    getPhy.Attribute = PHY_DEF_TX_DR;
    RegionGetPhyParam( LoRaMacRegion, &getPhy );
    LoRaMacParamsDefaults.ChannelsDatarate = getPhy.Param.Value;

    getPhy.Attribute = PHY_MAX_RX_WINDOW;
    RegionGetPhyParam( LoRaMacRegion, &getPhy );
    LoRaMacParamsDefaults.MaxRxWindow = getPhy.Param.Value;

    getPhy.Attribute = PHY_RECEIVE_DELAY1;
    RegionGetPhyParam( LoRaMacRegion, &getPhy );
    LoRaMacParamsDefaults.ReceiveDelay1 = getPhy.Param.Value;

    getPhy.Attribute = PHY_RECEIVE_DELAY2;
    RegionGetPhyParam( LoRaMacRegion, &getPhy );
    LoRaMacParamsDefaults.ReceiveDelay2 = getPhy.Param.Value;

    getPhy.Attribute = PHY_JOIN_ACCEPT_DELAY1;
    RegionGetPhyParam( LoRaMacRegion, &getPhy );
    LoRaMacParamsDefaults.JoinAcceptDelay1 = getPhy.Param.Value;

    getPhy.Attribute = PHY_JOIN_ACCEPT_DELAY2;
    RegionGetPhyParam( LoRaMacRegion, &getPhy );
    LoRaMacParamsDefaults.JoinAcceptDelay2 = getPhy.Param.Value;

    getPhy.Attribute = PHY_DEF_DR1_OFFSET;
    RegionGetPhyParam( LoRaMacRegion, &getPhy );
    LoRaMacParamsDefaults.Rx1DrOffset = getPhy.Param.Value;

    getPhy.Attribute = PHY_DEF_RX2_FREQUENCY;
    RegionGetPhyParam( LoRaMacRegion, &getPhy );
    LoRaMacParamsDefaults.Rx2Channel.Frequency = getPhy.Param.Value;

    getPhy.Attribute = PHY_DEF_RX2_DR;
    RegionGetPhyParam( LoRaMacRegion, &getPhy );
    LoRaMacParamsDefaults.Rx2Channel.Datarate = getPhy.Param.Value;

    getPhy.Attribute = PHY_DEF_UPLINK_DWELL_TIME;
    RegionGetPhyParam( LoRaMacRegion, &getPhy );
    LoRaMacParamsDefaults.UplinkDwellTime = getPhy.Param.Value;

    getPhy.Attribute = PHY_DEF_DOWNLINK_DWELL_TIME;
    RegionGetPhyParam( LoRaMacRegion, &getPhy );
    LoRaMacParamsDefaults.DownlinkDwellTime = getPhy.Param.Value;

    getPhy.Attribute = PHY_DEF_MAX_EIRP;
    RegionGetPhyParam( LoRaMacRegion, &getPhy );
    LoRaMacParamsDefaults.MaxEirp = getPhy.Param.Value;

    LoRaMacParamsDefaults.ChannelsNbRep = 1;

    RegionInitDefaults( LoRaMacRegion, INIT_TYPE_INIT );

    ResetMacParameters( );

    // Initialize timers
    TimerInit( &MacStateCheckTimer, OnMacStateCheckTimerEvent );
    TimerSetValue( &MacStateCheckTimer, MAC_STATE_CHECK_TIMEOUT );

    TimerInit( &TxDelayedTimer, OnTxDelayedTimerEvent );
    TimerInit( &RxWindowTimer1, OnRxWindow1TimerEvent );
    TimerInit( &RxWindowTimer2, OnRxWindow2TimerEvent );
    TimerInit( &AckTimeoutTimer, OnAckTimeoutTimerEvent );

    // Initialize Radio driver
    RadioEvents.TxDone = OnRadioTxDone;
    RadioEvents.RxDone = OnRadioRxDone;
    RadioEvents.RxError = OnRadioRxError;
    RadioEvents.TxTimeout = OnRadioTxTimeout;
    RadioEvents.RxTimeout = OnRadioRxTimeout;
    Radio.Init( &RadioEvents );

    // Random seed initialization
    srand1( Radio.Random( ) );

    PublicNetwork = true;
    Radio.SetPublicNetwork( PublicNetwork );
    Radio.Sleep( );

    return LORAMAC_STATUS_OK;
}

LoRaMacStatus_t LoRaMacQueryTxPossible( uint8_t size, LoRaMacTxInfo_t* txInfo )
{
    AdrNextParams_t adrNext;
    GetPhyParams_t getPhy;
    int8_t datarate = LoRaMacParamsDefaults.ChannelsDatarate;
    int8_t txPower = LoRaMacParamsDefaults.ChannelsTxPower;
    uint8_t fOptLen = MacCommandsBufferIndex + MacCommandsBufferToRepeatIndex;

    if( txInfo == NULL )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }

    // Setup ADR request
    adrNext.UpdateChanMask = false;
    adrNext.AdrEnabled = AdrCtrlOn;
    adrNext.AdrAckCounter = AdrAckCounter;
    adrNext.Datarate = LoRaMacParams.ChannelsDatarate;
    adrNext.TxPower = LoRaMacParams.ChannelsTxPower;

    // We call the function for information purposes only. We don't want to
    // apply the datarate, the tx power and the ADR ack counter.
    RegionAdrNext( LoRaMacRegion, &adrNext, &datarate, &txPower, &AdrAckCounter );

    // Setup PHY request
    getPhy.Datarate = LoRaMacParams.ChannelsDatarate;
    getPhy.Attribute = PHY_MAX_PAYLOAD;

    // Change request in case repeater is supported
    if( RepeaterSupport == true )
    {
        getPhy.Attribute = PHY_MAX_PAYLOAD_REPEATER;
    }
    RegionGetPhyParam( LoRaMacRegion, &getPhy );
    txInfo->CurrentPayloadSize = getPhy.Param.Value;

    // Verify if the fOpts fit into the maximum payload
    if( txInfo->CurrentPayloadSize >= fOptLen )
    {
        txInfo->MaxPossiblePayload = txInfo->CurrentPayloadSize - fOptLen;
    }
    else
    {
        txInfo->MaxPossiblePayload = 0;
        return LORAMAC_STATUS_MAC_CMD_LENGTH_ERROR;
    }

    // Verify if the payload fits into the maximum payload
    if( ValidatePayloadLength( size, datarate, 0 ) == false )
    {
        return LORAMAC_STATUS_LENGTH_ERROR;
    }

    // Verify if the fOpts and the payload fit into the maximum payload
    if( ValidatePayloadLength( size, datarate, fOptLen ) == false )
    {
        return LORAMAC_STATUS_MAC_CMD_LENGTH_ERROR;
    }

    return LORAMAC_STATUS_OK;
}

LoRaMacStatus_t LoRaMacMibGetRequestConfirm( MibRequestConfirm_t *mibGet )
{
    LoRaMacStatus_t status = LORAMAC_STATUS_OK;
    GetPhyParams_t getPhy;

    if( mibGet == NULL )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }

    switch( mibGet->Type )
    {
        case MIB_DEVICE_CLASS:
        {
            mibGet->Param.Class = LoRaMacDeviceClass;
            break;
        }
        case MIB_NETWORK_JOINED:
        {
            mibGet->Param.IsNetworkJoined = IsLoRaMacNetworkJoined;
            break;
        }
        case MIB_ADR:
        {
            mibGet->Param.AdrEnable = AdrCtrlOn;
            break;
        }
        case MIB_NET_ID:
        {
            mibGet->Param.NetID = LoRaMacNetID;
            break;
        }
        case MIB_DEV_ADDR:
        {
            mibGet->Param.DevAddr = LoRaMacDevAddr;
            break;
        }
        case MIB_NWK_SKEY:
        {
            mibGet->Param.NwkSKey = LoRaMacNwkSKey;
            break;
        }
        case MIB_APP_SKEY:
        {
            mibGet->Param.AppSKey = LoRaMacAppSKey;
            break;
        }
        case MIB_PUBLIC_NETWORK:
        {
            mibGet->Param.EnablePublicNetwork = PublicNetwork;
            break;
        }
        case MIB_REPEATER_SUPPORT:
        {
            mibGet->Param.EnableRepeaterSupport = RepeaterSupport;
            break;
        }
        case MIB_CHANNELS:
        {
            getPhy.Attribute = PHY_CHANNELS;
            RegionGetPhyParam( LoRaMacRegion, &getPhy );

            mibGet->Param.ChannelList = getPhy.Param.Channels;
            break;
        }
        case MIB_RX2_CHANNEL:
        {
            mibGet->Param.Rx2Channel = LoRaMacParams.Rx2Channel;
            break;
        }
        case MIB_RX2_DEFAULT_CHANNEL:
        {
            mibGet->Param.Rx2Channel = LoRaMacParamsDefaults.Rx2Channel;
            break;
        }
        case MIB_CHANNELS_DEFAULT_MASK:
        {
            getPhy.Attribute = PHY_CHANNELS_DEFAULT_MASK;
            RegionGetPhyParam( LoRaMacRegion, &getPhy );

            mibGet->Param.ChannelsDefaultMask = getPhy.Param.ChannelsMask;
            break;
        }
        case MIB_CHANNELS_MASK:
        {
            getPhy.Attribute = PHY_CHANNELS_MASK;
            RegionGetPhyParam( LoRaMacRegion, &getPhy );

            mibGet->Param.ChannelsMask = getPhy.Param.ChannelsMask;
            break;
        }
        case MIB_CHANNELS_NB_REP:
        {
            mibGet->Param.ChannelNbRep = LoRaMacParams.ChannelsNbRep;
            break;
        }
        case MIB_MAX_RX_WINDOW_DURATION:
        {
            mibGet->Param.MaxRxWindow = LoRaMacParams.MaxRxWindow;
            break;
        }
        case MIB_RECEIVE_DELAY_1:
        {
            mibGet->Param.ReceiveDelay1 = LoRaMacParams.ReceiveDelay1;
            break;
        }
        case MIB_RECEIVE_DELAY_2:
        {
            mibGet->Param.ReceiveDelay2 = LoRaMacParams.ReceiveDelay2;
            break;
        }
        case MIB_JOIN_ACCEPT_DELAY_1:
        {
            mibGet->Param.JoinAcceptDelay1 = LoRaMacParams.JoinAcceptDelay1;
            break;
        }
        case MIB_JOIN_ACCEPT_DELAY_2:
        {
            mibGet->Param.JoinAcceptDelay2 = LoRaMacParams.JoinAcceptDelay2;
            break;
        }
        case MIB_CHANNELS_DEFAULT_DATARATE:
        {
            mibGet->Param.ChannelsDefaultDatarate = LoRaMacParamsDefaults.ChannelsDatarate;
            break;
        }
        case MIB_CHANNELS_DATARATE:
        {
            mibGet->Param.ChannelsDatarate = LoRaMacParams.ChannelsDatarate;
            break;
        }
        case MIB_CHANNELS_DEFAULT_TX_POWER:
        {
            mibGet->Param.ChannelsDefaultTxPower = LoRaMacParamsDefaults.ChannelsTxPower;
            break;
        }
        case MIB_CHANNELS_TX_POWER:
        {
            mibGet->Param.ChannelsTxPower = LoRaMacParams.ChannelsTxPower;
            break;
        }
        case MIB_UPLINK_COUNTER:
        {
            mibGet->Param.UpLinkCounter = UpLinkCounter;
            break;
        }
        case MIB_DOWNLINK_COUNTER:
        {
            mibGet->Param.DownLinkCounter = DownLinkCounter;
            break;
        }
        case MIB_MULTICAST_CHANNEL:
        {
            mibGet->Param.MulticastList = MulticastChannels;
            break;
        }
        default:
            status = LORAMAC_STATUS_SERVICE_UNKNOWN;
            break;
    }

    return status;
}

LoRaMacStatus_t LoRaMacMibSetRequestConfirm( MibRequestConfirm_t *mibSet )
{
    LoRaMacStatus_t status = LORAMAC_STATUS_OK;
    ChanMaskSetParams_t chanMaskSet;
    VerifyParams_t verify;

    if( mibSet == NULL )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }
    if( ( LoRaMacState & LORAMAC_TX_RUNNING ) == LORAMAC_TX_RUNNING )
    {
        return LORAMAC_STATUS_BUSY;
    }

    switch( mibSet->Type )
    {
        case MIB_DEVICE_CLASS:
        {
            LoRaMacDeviceClass = mibSet->Param.Class;
            switch( LoRaMacDeviceClass )
            {
                case CLASS_A:
                {
                    // Set the radio into sleep to setup a defined state
                    Radio.Sleep( );
                    break;
                }
                case CLASS_B:
                {
                    break;
                }
                case CLASS_C:
                {
                    // Set the NodeAckRequested indicator to default
                    NodeAckRequested = false;
                    OnRxWindow2TimerEvent( );
                    break;
                }
            }
            break;
        }
        case MIB_NETWORK_JOINED:
        {
            IsLoRaMacNetworkJoined = mibSet->Param.IsNetworkJoined;
            break;
        }
        case MIB_ADR:
        {
            AdrCtrlOn = mibSet->Param.AdrEnable;
            break;
        }
        case MIB_NET_ID:
        {
            LoRaMacNetID = mibSet->Param.NetID;
            break;
        }
        case MIB_DEV_ADDR:
        {
            LoRaMacDevAddr = mibSet->Param.DevAddr;
            break;
        }
        case MIB_NWK_SKEY:
        {
            if( mibSet->Param.NwkSKey != NULL )
            {
                memcpy1( LoRaMacNwkSKey, mibSet->Param.NwkSKey,
                               sizeof( LoRaMacNwkSKey ) );
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_APP_SKEY:
        {
            if( mibSet->Param.AppSKey != NULL )
            {
                memcpy1( LoRaMacAppSKey, mibSet->Param.AppSKey,
                               sizeof( LoRaMacAppSKey ) );
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_PUBLIC_NETWORK:
        {
            PublicNetwork = mibSet->Param.EnablePublicNetwork;
            Radio.SetPublicNetwork( PublicNetwork );
            break;
        }
        case MIB_REPEATER_SUPPORT:
        {
             RepeaterSupport = mibSet->Param.EnableRepeaterSupport;
            break;
        }
        case MIB_RX2_CHANNEL:
        {
            LoRaMacParams.Rx2Channel = mibSet->Param.Rx2Channel;
            break;
        }
        case MIB_RX2_DEFAULT_CHANNEL:
        {
            LoRaMacParamsDefaults.Rx2Channel = mibSet->Param.Rx2DefaultChannel;
            break;
        }
        case MIB_CHANNELS_DEFAULT_MASK:
        {
            chanMaskSet.ChannelsMaskIn = mibSet->Param.ChannelsMask;
            chanMaskSet.ChannelsMaskType = CHANNELS_DEFAULT_MASK;

            if( RegionChanMaskSet( LoRaMacRegion, &chanMaskSet ) == false )
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_CHANNELS_MASK:
        {
            chanMaskSet.ChannelsMaskIn = mibSet->Param.ChannelsMask;
            chanMaskSet.ChannelsMaskType = CHANNELS_MASK;

            if( RegionChanMaskSet( LoRaMacRegion, &chanMaskSet ) == false )
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_CHANNELS_NB_REP:
        {
            if( ( mibSet->Param.ChannelNbRep >= 1 ) &&
                ( mibSet->Param.ChannelNbRep <= 15 ) )
            {
                LoRaMacParams.ChannelsNbRep = mibSet->Param.ChannelNbRep;
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_MAX_RX_WINDOW_DURATION:
        {
            LoRaMacParams.MaxRxWindow = mibSet->Param.MaxRxWindow;
            break;
        }
        case MIB_RECEIVE_DELAY_1:
        {
            LoRaMacParams.ReceiveDelay1 = mibSet->Param.ReceiveDelay1;
            break;
        }
        case MIB_RECEIVE_DELAY_2:
        {
            LoRaMacParams.ReceiveDelay2 = mibSet->Param.ReceiveDelay2;
            break;
        }
        case MIB_JOIN_ACCEPT_DELAY_1:
        {
            LoRaMacParams.JoinAcceptDelay1 = mibSet->Param.JoinAcceptDelay1;
            break;
        }
        case MIB_JOIN_ACCEPT_DELAY_2:
        {
            LoRaMacParams.JoinAcceptDelay2 = mibSet->Param.JoinAcceptDelay2;
            break;
        }
        case MIB_CHANNELS_DEFAULT_DATARATE:
        {
            verify.Datarate = mibSet->Param.ChannelsDefaultDatarate;

            if( RegionVerify( LoRaMacRegion, &verify, PHY_DEF_TX_DR ) == true )
            {
                LoRaMacParamsDefaults.ChannelsDatarate = verify.Datarate;
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_CHANNELS_DATARATE:
        {
            verify.Datarate = mibSet->Param.ChannelsDatarate;

            if( RegionVerify( LoRaMacRegion, &verify, PHY_TX_DR ) == true )
            {
                LoRaMacParams.ChannelsDatarate = verify.Datarate;
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_CHANNELS_DEFAULT_TX_POWER:
        {
            verify.TxPower = mibSet->Param.ChannelsDefaultTxPower;

            if( RegionVerify( LoRaMacRegion, &verify, PHY_DEF_TX_POWER ) == true )
            {
                LoRaMacParamsDefaults.ChannelsTxPower = verify.TxPower;
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_CHANNELS_TX_POWER:
        {
            verify.TxPower = mibSet->Param.ChannelsTxPower;

            if( RegionVerify( LoRaMacRegion, &verify, PHY_TX_POWER ) == true )
            {
                LoRaMacParams.ChannelsTxPower = verify.TxPower;
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_UPLINK_COUNTER:
        {
            UpLinkCounter = mibSet->Param.UpLinkCounter;
            break;
        }
        case MIB_DOWNLINK_COUNTER:
        {
            DownLinkCounter = mibSet->Param.DownLinkCounter;
            break;
        }
        default:
            status = LORAMAC_STATUS_SERVICE_UNKNOWN;
            break;
    }

    return status;
}

LoRaMacStatus_t LoRaMacChannelAdd( uint8_t id, ChannelParams_t params )
{
    ChannelAddParams_t channelAdd;

    // Validate if the MAC is in a correct state
    if( ( LoRaMacState & LORAMAC_TX_RUNNING ) == LORAMAC_TX_RUNNING )
    {
        if( ( LoRaMacState & LORAMAC_TX_CONFIG ) != LORAMAC_TX_CONFIG )
        {
            return LORAMAC_STATUS_BUSY;
        }
    }

    channelAdd.NewChannel = &params;
    channelAdd.ChannelId = id;

    return RegionChannelAdd( LoRaMacRegion, &channelAdd );
}

LoRaMacStatus_t LoRaMacChannelRemove( uint8_t id )
{
    ChannelRemoveParams_t channelRemove;

    if( ( LoRaMacState & LORAMAC_TX_RUNNING ) == LORAMAC_TX_RUNNING )
    {
        if( ( LoRaMacState & LORAMAC_TX_CONFIG ) != LORAMAC_TX_CONFIG )
        {
            return LORAMAC_STATUS_BUSY;
        }
    }

    channelRemove.ChannelId = id;

    if( RegionChannelsRemove( LoRaMacRegion, &channelRemove ) == false )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }
    return LORAMAC_STATUS_OK;
}

LoRaMacStatus_t LoRaMacMulticastChannelLink( MulticastParams_t *channelParam )
{
    if( channelParam == NULL )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }
    if( ( LoRaMacState & LORAMAC_TX_RUNNING ) == LORAMAC_TX_RUNNING )
    {
        return LORAMAC_STATUS_BUSY;
    }

    // Reset downlink counter
    channelParam->DownLinkCounter = 0;

    if( MulticastChannels == NULL )
    {
        // New node is the fist element
        MulticastChannels = channelParam;
    }
    else
    {
        MulticastParams_t *cur = MulticastChannels;

        // Search the last node in the list
        while( cur->Next != NULL )
        {
            cur = cur->Next;
        }
        // This function always finds the last node
        cur->Next = channelParam;
    }

    return LORAMAC_STATUS_OK;
}

LoRaMacStatus_t LoRaMacMulticastChannelUnlink( MulticastParams_t *channelParam )
{
    if( channelParam == NULL )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }
    if( ( LoRaMacState & LORAMAC_TX_RUNNING ) == LORAMAC_TX_RUNNING )
    {
        return LORAMAC_STATUS_BUSY;
    }

    if( MulticastChannels != NULL )
    {
        if( MulticastChannels == channelParam )
        {
          // First element
          MulticastChannels = channelParam->Next;
        }
        else
        {
            MulticastParams_t *cur = MulticastChannels;

            // Search the node in the list
            while( cur->Next && cur->Next != channelParam )
            {
                cur = cur->Next;
            }
            // If we found the node, remove it
            if( cur->Next )
            {
                cur->Next = channelParam->Next;
            }
        }
        channelParam->Next = NULL;
    }

    return LORAMAC_STATUS_OK;
}

LoRaMacStatus_t LoRaMacMlmeRequest( MlmeReq_t *mlmeRequest )
{
    LoRaMacStatus_t status = LORAMAC_STATUS_SERVICE_UNKNOWN;
    LoRaMacHeader_t macHdr;
    AlternateDrParams_t altDr;
    VerifyParams_t verify;
    GetPhyParams_t getPhy;

    if( mlmeRequest == NULL )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }
    if( ( LoRaMacState & LORAMAC_TX_RUNNING ) == LORAMAC_TX_RUNNING )
    {
        return LORAMAC_STATUS_BUSY;
    }

    memset1( ( uint8_t* ) &MlmeConfirm, 0, sizeof( MlmeConfirm ) );

    MlmeConfirm.Status = LORAMAC_EVENT_INFO_STATUS_ERROR;

    switch( mlmeRequest->Type )
    {
        case MLME_JOIN:
        {
            if( ( LoRaMacState & LORAMAC_TX_DELAYED ) == LORAMAC_TX_DELAYED )
            {
                return LORAMAC_STATUS_BUSY;
            }

            if( ( mlmeRequest->Req.Join.DevEui == NULL ) ||
                ( mlmeRequest->Req.Join.AppEui == NULL ) ||
                ( mlmeRequest->Req.Join.AppKey == NULL ) ||
                ( mlmeRequest->Req.Join.NbTrials == 0 ) )
            {
                return LORAMAC_STATUS_PARAMETER_INVALID;
            }

            // Verify the parameter NbTrials for the join procedure
            verify.NbJoinTrials = mlmeRequest->Req.Join.NbTrials;

            if( RegionVerify( LoRaMacRegion, &verify, PHY_NB_JOIN_TRIALS ) == false )
            {
                // Value not supported, get default
                getPhy.Attribute = PHY_DEF_NB_JOIN_TRIALS;
                RegionGetPhyParam( LoRaMacRegion, &getPhy );
                mlmeRequest->Req.Join.NbTrials = ( uint8_t ) getPhy.Param.Value;
            }

            LoRaMacFlags.Bits.MlmeReq = 1;
            MlmeConfirm.MlmeRequest = mlmeRequest->Type;

            LoRaMacDevEui = mlmeRequest->Req.Join.DevEui;
            LoRaMacAppEui = mlmeRequest->Req.Join.AppEui;
            LoRaMacAppKey = mlmeRequest->Req.Join.AppKey;
            MaxJoinRequestTrials = mlmeRequest->Req.Join.NbTrials;

            // Reset variable JoinRequestTrials
            JoinRequestTrials = 0;

            // Setup header information
            macHdr.Value = 0;
            macHdr.Bits.MType  = FRAME_TYPE_JOIN_REQ;

            ResetMacParameters( );

            altDr.NbTrials = JoinRequestTrials + 1;

            LoRaMacParams.ChannelsDatarate = RegionAlternateDr( LoRaMacRegion, &altDr );

            status = Send( &macHdr, 0, NULL, 0 );
            break;
        }
        case MLME_LINK_CHECK:
        {
            LoRaMacFlags.Bits.MlmeReq = 1;
            // LoRaMac will send this command piggy-pack
            MlmeConfirm.MlmeRequest = mlmeRequest->Type;

            status = AddMacCommand( MOTE_MAC_LINK_CHECK_REQ, 0, 0 );
            break;
        }
        case MLME_TXCW:
        {
            MlmeConfirm.MlmeRequest = mlmeRequest->Type;
            LoRaMacFlags.Bits.MlmeReq = 1;
            status = SetTxContinuousWave( mlmeRequest->Req.TxCw.Timeout );
            break;
        }
        default:
            break;
    }

    if( status != LORAMAC_STATUS_OK )
    {
        NodeAckRequested = false;
        LoRaMacFlags.Bits.MlmeReq = 0;
    }

    return status;
}

LoRaMacStatus_t LoRaMacMcpsRequest( McpsReq_t *mcpsRequest )
{
    LoRaMacStatus_t status = LORAMAC_STATUS_SERVICE_UNKNOWN;
    LoRaMacHeader_t macHdr;
    VerifyParams_t verify;
    uint8_t fPort = 0;
    void *fBuffer;
    uint16_t fBufferSize;
    int8_t datarate;
    bool readyToSend = false;

    if( mcpsRequest == NULL )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }
    if( ( ( LoRaMacState & LORAMAC_TX_RUNNING ) == LORAMAC_TX_RUNNING ) ||
        ( ( LoRaMacState & LORAMAC_TX_DELAYED ) == LORAMAC_TX_DELAYED ) )
    {
        return LORAMAC_STATUS_BUSY;
    }

    macHdr.Value = 0;
    memset1 ( ( uint8_t* ) &McpsConfirm, 0, sizeof( McpsConfirm ) );
    McpsConfirm.Status = LORAMAC_EVENT_INFO_STATUS_ERROR;

    switch( mcpsRequest->Type )
    {
        case MCPS_UNCONFIRMED:
        {
            readyToSend = true;
            AckTimeoutRetries = 1;

            macHdr.Bits.MType = FRAME_TYPE_DATA_UNCONFIRMED_UP;
            fPort = mcpsRequest->Req.Unconfirmed.fPort;
            fBuffer = mcpsRequest->Req.Unconfirmed.fBuffer;
            fBufferSize = mcpsRequest->Req.Unconfirmed.fBufferSize;
            datarate = mcpsRequest->Req.Unconfirmed.Datarate;
            break;
        }
        case MCPS_CONFIRMED:
        {
            readyToSend = true;
            AckTimeoutRetriesCounter = 1;
            AckTimeoutRetries = mcpsRequest->Req.Confirmed.NbTrials;

            macHdr.Bits.MType = FRAME_TYPE_DATA_CONFIRMED_UP;
            fPort = mcpsRequest->Req.Confirmed.fPort;
            fBuffer = mcpsRequest->Req.Confirmed.fBuffer;
            fBufferSize = mcpsRequest->Req.Confirmed.fBufferSize;
            datarate = mcpsRequest->Req.Confirmed.Datarate;
            break;
        }
        case MCPS_PROPRIETARY:
        {
            readyToSend = true;
            AckTimeoutRetries = 1;

            macHdr.Bits.MType = FRAME_TYPE_PROPRIETARY;
            fBuffer = mcpsRequest->Req.Proprietary.fBuffer;
            fBufferSize = mcpsRequest->Req.Proprietary.fBufferSize;
            datarate = mcpsRequest->Req.Proprietary.Datarate;
            break;
        }
        default:
            break;
    }

    if( readyToSend == true )
    {
        if( AdrCtrlOn == false )
        {
            verify.Datarate = datarate;

            if( RegionVerify( LoRaMacRegion, &verify, PHY_TX_DR ) == true )
            {
                LoRaMacParams.ChannelsDatarate = verify.Datarate;
            }
            else
            {
                return LORAMAC_STATUS_PARAMETER_INVALID;
            }
        }

        status = Send( &macHdr, fPort, fBuffer, fBufferSize );
        if( status == LORAMAC_STATUS_OK )
        {
            McpsConfirm.McpsRequest = mcpsRequest->Type;
            LoRaMacFlags.Bits.McpsReq = 1;
        }
        else
        {
            NodeAckRequested = false;
        }
    }

    return status;
}

void LoRaMacTestRxWindowsOn( bool enable )
{
    IsRxWindowsEnabled = enable;
}

void LoRaMacTestSetMic( uint16_t txPacketCounter )
{
    UpLinkCounter = txPacketCounter;
    IsUpLinkCounterFixed = true;
}

void LoRaMacTestSetDutyCycleOn( bool enable )
{
    VerifyParams_t verify;

    verify.DutyCycle = enable;

    if( RegionVerify( LoRaMacRegion, &verify, PHY_DUTY_CYCLE ) == true )
    {
        DutyCycleOn = enable;
    }
}

void LoRaMacTestSetChannel( uint8_t channel )
{
    Channel = channel;
}
