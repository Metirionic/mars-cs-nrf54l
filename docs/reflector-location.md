# Set a reflector's location

The reflector exposes X and Y as readable and writable Bluetooth characteristics
in both RAS and IPT builds. Each coordinate is a signed integer in millimetres,
encoded as UTF-8 decimal text. For example, `1500` means 1.5 metres and `-250`
means -0.25 metres. Choose a shared origin and axis directions for your setup.

## Set coordinates with nRF Connect for Mobile

1. Flash a reflector build that includes the location service. Turn off the
   initiator while configuring: the reflector supports one connection, so an
   initiator connection prevents the phone from connecting.
2. Scan in nRF Connect for Mobile and connect to **Nordic CS Reflector**.
3. Discover services and expand the custom location service below. It may
   appear as an unknown service; the characteristic user descriptions identify
   **x** and **y** (read the Characteristic User Description descriptors).
4. Write each characteristic using **Write Request** and the **Text / UTF-8**
   value format. Enter a whole number such as `1500`, without spaces or units.
5. Read each characteristic as text to confirm the saved value. Disconnect and
   restore power to the initiator when finished.

| Attribute | UUID |
|-----------|------|
| Location service | `a00c0000-7ea1-4b73-9bb1-6f54940c3a10` |
| X | `a00c0001-7ea1-4b73-9bb1-6f54940c3a10` |
| Y | `a00c0002-7ea1-4b73-9bb1-6f54940c3a10` |

Values range from `-2147483648` to `2147483647` millimetres. Both default to
`0` until configured. A successful write response means that coordinate has
been saved to internal nonvolatile storage; it survives disconnection, reboot,
and power loss. X and Y save independently. A full chip erase or recovery
clears the stored values. If a write fails, read the characteristic and retry.

Configuration uses an ordinary BLE connection without pairing. Any connected
central can read or change these values. The coordinates are stored metadata;
the initiator does not yet fetch them or include them in the UART measurement
stream.
