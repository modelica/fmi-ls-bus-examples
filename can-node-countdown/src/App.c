#include "App.h"

#include <stdlib.h>

#include "fmi3LsBusUtilCan.h"

#include "Logging.h"


/**
 * \brief Identifies variables of this FMU with their value references.
 */
typedef enum
{
    FMU_VAR_RX_DATA = 0,
    FMU_VAR_TX_DATA = 1,
    FMU_VAR_RX_CLOCK = 2,
    FMU_VAR_TX_CLOCK = 3,

    FMU_VAR_CAN_BUS_NOTIFICATIONS = 128,
    FMU_VAR_SIMULATION_TIME = 1024
} FmuVariables;




/**
 * \brief Instance-specific data of this FMU.
 */
struct AppType
{
    fmi3Byte RxBuffer[2048];
    fmi3LsBusUtilBufferInfo RxBufferInfo;
    fmi3Clock RxClock;

    fmi3Byte TxBuffer[2048];
    fmi3LsBusUtilBufferInfo TxBufferInfo;
    fmi3Clock TxClock;
    fmi3IntervalQualifier TxClockQualifier;

    fmi3UInt64 TxClockCounter;
    fmi3UInt64 TxClockResolution;

    fmi3Float64 NextTransmitTime;
    fmi3Float64 SimulationTime;

    fmi3Float64 TransmitInterval;
};


AppType* App_Instantiate(void)
{
    AppType* app = calloc(1, sizeof(AppType));
    if (app == NULL)
    {
        return NULL;
    }

    // Initialize transmit and receive buffers
    FMI3_LS_BUS_BUFFER_INFO_INIT(&app->RxBufferInfo, app->RxBuffer, sizeof(app->RxBuffer));
    FMI3_LS_BUS_BUFFER_INFO_INIT(&app->TxBufferInfo, app->TxBuffer, sizeof(app->TxBuffer));

    // Create bus configuration operations
    FMI3_LS_BUS_CAN_CREATE_OP_CONFIGURATION_CAN_BAUDRATE(&app->TxBufferInfo, 100000);
    FMI3_LS_BUS_CAN_CREATE_OP_CONFIGURATION_ARBITRATION_LOST_BEHAVIOR(
        &app->TxBufferInfo,
        FMI3_LS_BUS_CAN_CONFIG_PARAM_ARBITRATION_LOST_BEHAVIOR_BUFFER_AND_RETRANSMIT);
    app->TxClock = fmi3ClockActive;

    app->SimulationTime = 0.0;

    // Schedule next transmission

    app->TxClockQualifier = fmi3IntervalChanged;
    app->TxClockCounter = 3;
    app->TxClockResolution = 10;

    app->TransmitInterval =  (fmi3Float64) app->TxClockCounter / app->TxClockResolution;

    app->NextTransmitTime = app->SimulationTime + app->TransmitInterval;

    return app;
}


void App_Free(AppType* instance)
{
    free(instance);
}


bool App_DoStep(FmuInstance* instance, fmi3Float64 currentTime, fmi3Float64 targetTime)
{
    instance->App->SimulationTime = targetTime;
    return false;
}


void App_EvaluateDiscreteStates(FmuInstance* instance)
{
    (void)instance;
}


static void App_ProcessRxBuffer(FmuInstance* instance)
{
    if (instance->App->RxClock != fmi3ClockActive)
        return;

    // Read all bus operations from the RX buffer
    fmi3LsBusOperationHeader* operation = NULL;
    while (FMI3_LS_BUS_READ_NEXT_OPERATION(&instance->App->RxBufferInfo, operation))
    {
        if (operation->opCode == FMI3_LS_BUS_CAN_OP_CAN_TRANSMIT)
        {
            const fmi3LsBusCanOperationCanTransmit* transmitOp = (fmi3LsBusCanOperationCanTransmit*)operation;
            LogFmuMessage(instance, fmi3OK, "Info", "Received CAN frame with ID %u and length %u", transmitOp->id,
                          transmitOp->dataLength);
        }
        else if (operation->opCode == FMI3_LS_BUS_CAN_OP_CONFIGURATION ||
                 operation->opCode == FMI3_LS_BUS_CAN_OP_STATUS ||
                 operation->opCode == FMI3_LS_BUS_CAN_OP_CONFIRM ||
                 operation->opCode == FMI3_LS_BUS_CAN_OP_BUS_ERROR ||
                 operation->opCode == FMI3_LS_BUS_CAN_OP_ARBITRATION_LOST)
        {
            // Ignore
        }
        else
        {
            LogFmuMessage(instance, fmi3OK, "Warning", "Received unknown bus operation");
        }
    }
}


static void App_PrepareTxBuffer(FmuInstance* instance)
{
    if (instance->App->TxClock != fmi3ClockActive)
        return;

    const fmi3LsBusCanId id = 0x1;
    const fmi3Byte data[4] = {1, 2, 3, 4};

    LogFmuMessage(instance, fmi3OK, "Info", "Transmitting CAN frame with ID %u", id);

    FMI3_LS_BUS_CAN_CREATE_OP_CAN_TRANSMIT(&instance->App->TxBufferInfo, id, FMI3_LS_BUS_FALSE, FMI3_LS_BUS_FALSE, sizeof data, data);

    if (!instance->App->TxBufferInfo.status)
    {
        LogFmuMessage(instance, fmi3Warning, "Warning", "Failed to transmit CAN frame: Insufficient buffer space");
    }
}


void App_UpdateDiscreteStates(FmuInstance* instance)
{
    if (instance->App->RxClock == fmi3ClockActive)
    {
        App_ProcessRxBuffer(instance);

        // Deactivate RX clock and clear RX buffer since all operations should have been processed
        instance->App->RxClock = fmi3ClockInactive;
        FMI3_LS_BUS_BUFFER_INFO_RESET(&instance->App->RxBufferInfo);
    }

    if (instance->App->TxClock == fmi3ClockActive)
    {
        instance->App->TxClockQualifier = fmi3IntervalChanged;
        instance->App->NextTransmitTime = instance->App->NextTransmitTime + instance->App->TransmitInterval;

        // Deactivate TX clock and clear TX buffer since both should have been retrieved by this time
        instance->App->TxClock = fmi3ClockInactive;
        FMI3_LS_BUS_BUFFER_INFO_RESET(&instance->App->TxBufferInfo);
    }
}


bool App_SetBoolean(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Boolean value)
{
    if (valueReference == FMU_VAR_CAN_BUS_NOTIFICATIONS)
    {
        LogFmuMessage(instance, fmi3OK, "Info", "Set Can_BusNotifications to %u", value);
        return true;
    }

    return false;
}


bool App_SetFloat64(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Float64 value)
{
    (void)instance;
    (void)valueReference;
    (void)value;
    return false;
}


bool App_GetFloat64(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Float64* value)
{
    if (valueReference == FMU_VAR_SIMULATION_TIME)
    {
        *value = instance->App->SimulationTime;
        return true;
    }

    return false;
}


bool App_SetBinary(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Binary value, size_t valueLength)
{
    if (valueReference == FMU_VAR_RX_DATA)
    {
        LogFmuMessage(instance, fmi3OK, "Trace", "Set RX buffer of %llu bytes", valueLength);

        FmuState state = instance->State;
        if ((state != FMU_STATE_EVENT_MODE || instance->App->RxClock != fmi3ClockActive) && state != FMU_STATE_INITIALIZATION_MODE) {
            LogFmuMessage(instance, fmi3Error, "Error", "Setting clocked binary variable in current state is not allowed");
            return false;
        }

        FMI3_LS_BUS_BUFFER_WRITE(&instance->App->RxBufferInfo, value, valueLength);
        return true;
    }

    return false;
}

bool App_GetBinary(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Binary* value, size_t* valueLength)
{
    if (valueReference == FMU_VAR_TX_DATA)
    {
        FmuState state = instance->State;
        if ((state != FMU_STATE_EVENT_MODE || instance->App->TxClock != fmi3ClockActive) && state != FMU_STATE_INITIALIZATION_MODE) {
            LogFmuMessage(instance, fmi3Error, "Error", "Getting clocked binary variable in current state is not allowed");
            return false;
        }

        // Build the CAN transmit frame on-demand
        App_PrepareTxBuffer(instance);

        *value = FMI3_LS_BUS_BUFFER_START(&instance->App->TxBufferInfo);
        *valueLength = FMI3_LS_BUS_BUFFER_LENGTH(&instance->App->TxBufferInfo);
        LogFmuMessage(instance, fmi3OK, "Trace", "Get TX buffer of %llu bytes", *valueLength);
        return true;
    }

    return false;
}


bool App_SetClock(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Clock value)
{
    FmuState state = instance->State;
    if (state != FMU_STATE_EVENT_MODE) {
        LogFmuMessage(instance, fmi3Error, "Error", "Setting clock variable in current state is not allowed");
        return false;
    }

    if (valueReference == FMU_VAR_RX_CLOCK)
    {
        LogFmuMessage(instance, fmi3OK, "Trace", "Set RX clock to %u", value);
        instance->App->RxClock = value;

        return true;
    }
    else if (valueReference == FMU_VAR_TX_CLOCK)
    {
        LogFmuMessage(instance, fmi3OK, "Trace", "Set TX clock to %d", value);
        instance->App->TxClock = value;

        return true;
    }

    return false;
}


bool App_GetClock(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Clock* value)
{
    return false;
}


bool App_GetIntervalFraction(FmuInstance* instance,
                             fmi3ValueReference valueReference,
                             fmi3UInt64* counter,
                             fmi3UInt64* resolution,
                             fmi3IntervalQualifier* qualifier)
{
    if (valueReference == FMU_VAR_TX_CLOCK)
    {
        *qualifier = instance->App->TxClockQualifier;
        if (instance->App->TxClockQualifier == fmi3IntervalChanged)
        {
            *counter = instance->App->TxClockCounter;
            *resolution = instance->App->TxClockResolution;
            instance->App->TxClockQualifier = fmi3IntervalUnchanged;
        }
        return true;
    }

    return false;
}
