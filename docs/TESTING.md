# Alpha Testing Guide

## 1. Safe boot

Expected:

- OEM mode
- H1/H2/H3 LOW
- terminal available
- no automatic heating request

## 2. Sensor discovery

```text
sensor scan
sensor setup
```

Assign only physically present sensors.

## 3. Sensor combinations

Verify separately:

- LOW only
- HIGH only
- LOW + HIGH
- optional OUTSIDE
- optional SAFETY

## 4. Virtual cabin temperature

With LOW and HIGH assigned, verify the configured weighting.

Default:

- LOW: 40 %
- HIGH: 60 %

## 5. Safety-Lock

Fault injection should verify:

- H1/H2/H3 go LOW
- own heating request is blocked
- terminal remains available
- diagnostics can be read
- no automatic heating restart occurs

## 6. WLAN

```text
wifi on
wifi status
wifi off
```

SSID must be `ZHC`.

## 7. Web login

Verify:

- unauthorized access is rejected
- temporary first-login password works
- password change is mandatory
- logout invalidates session
- inactivity expires session

## 8. OTA

Before firmware writing confirm:

- OEM mode
- requested stage 0
- active stage 0
- H1/H2/H3 LOW

## 9. Regression

After firmware changes repeat:

- safe boot
- sensor assignment
- control-start rejection without cabin sensor
- Safety-Lock behavior
- terminal access during fault
- WLAN on/off
- login
- OTA safe-state entry
