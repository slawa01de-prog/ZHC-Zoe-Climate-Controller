# Safety Notice

> [!CAUTION]
> **ALPHA SOFTWARE – TESTING AT YOUR OWN RISK**

ZHC is experimental development software. It is not a certified automotive
control system and does not replace OEM safety mechanisms, interlocks,
diagnostics, or protection functions.

## Current output scope

`H1`, `H2`, and `H3` are **3.3 V logic / bench-test outputs only** in the
current Alpha firmware.

They are not intended as direct drive outputs for high-voltage PTC heaters,
contactors, HV relays, traction-battery circuits, vehicle safety interlocks,
or other high-energy loads.

## Conservative development sequence

1. Code review
2. ESP32-C3 bench operation
3. LEDs / logic analyzer / low-energy test load
4. Sensor simulation and fault injection
5. Controlled interface validation

## Safety-Lock

A fault may lock out ZHC's own heating request while leaving the local service
terminal available for diagnosis. Reading a diagnostic is not equivalent to
clearing the Safety-Lock.

## Certification

This implementation has not been validated as an automotive functional-safety
system. Testing and use are performed at the user's own responsibility and risk.
