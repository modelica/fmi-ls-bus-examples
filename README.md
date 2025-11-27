# Demo FMUs for FMI-LS-BUS

This directory contains demo FMUs implementing the FMI-LS-BUS.

## Demos for CAN

The following demos are provided for the CAN bus:

- `can-bus-simulation`:
    A simple Bus Simulation FMU for connection two CAN nodes.
- `can-node-triggered-output`:
    A CAN node sending a periodic frame using a triggered output clock.

## Demos for FlexRay

The following demos are provided for the FlexRay bus:

- `flexray-bus-simulation`:
    A simple Bus Simulation FMU for connection two FlexRay nodes.
- `flexray-node`:
    A FlexRay node periodically sends a FlexRay message in slots 1 and 3 or 2 and 4 of each cycle.
