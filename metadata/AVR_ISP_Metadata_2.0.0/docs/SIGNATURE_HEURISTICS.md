<!-- Copyright (C) 2026 Flyandance JZ - GPL-3.0-or-later -->

# Signature-Derived Verification Heuristics

## Flash capacity

For the classic AVR records examined here, the low nibble of the second signature byte behaves as a Flash-capacity code:

```cpp
std::optional<std::uint32_t> flashSizeFromSignature(const std::array<std::uint8_t, 3>& signature)
{
    const std::uint8_t code = signature[1] & 0x0F;
    if (code > 12) {
        return std::nullopt;
    }
    return std::uint32_t{1} << (10 + code);
}
```

Examples:

- `1E 94 xx`: `1 << 14` = 16,384 bytes.
- `1E 95 xx`: `1 << 15` = 32,768 bytes.
- `1E 97 xx`: `1 << 17` = 131,072 bytes.

Validation in this release:

- 191 total records checked.
- 189 matches.
- 2 failures: AT89S51 and AT89S52, both non-AVR devices.
- 189 of 189 AVR or AVR-compatible records matched.

This is strong empirical confirmation for the classic AVR set, not proof that every future or third-party signature follows the same encoding.

## Flash page size

The supplied decoder is implemented exactly as a verification rule:

```cpp
std::uint16_t pageSizeFromSignature(const std::array<std::uint8_t, 3>& signature)
{
    switch (signature[1] & 0x0F) {
    case 0:
    case 1:
        return 32;
    case 2:
    case 3:
        return 64;
    case 4:
    case 5:
        return 128;
    case 6:
    case 7:
        return 256;
    default:
        return 64;
    }
}
```

Validation in this release:

- 176 records had a sourced Flash page size.
- 161 matched the heuristic.
- 15 did not match.
- Match rate: 91.48 percent.

Known exceptions:

- AT90CAN32
- AT90USB82
- ATA6616C
- ATmega2560
- ATmega2561
- ATmega2564RFR2
- ATmega256RFR2
- ATmega64HVE2
- ATmega8HVA
- ATmega8U2
- ATtiny1634
- ATtiny1634R
- ATtiny441
- ATtiny841
- ATtiny87

Therefore page size must come from an official source or a mature implementation. The heuristic should only flag suspicious data or provide a last-resort diagnostic guess when the target is otherwise unknown.

## Recommended verification logic

```text
1. Read the signature three times and require stable bytes.
2. Match all database records with that signature.
3. Compare selected Flash size with the capacity-code guess.
4. Compare selected page size with the page heuristic.
5. Treat mismatches as warnings, not automatic corrections.
6. Require exact user selection when multiple records share a signature.
7. Block fuse and lock writes until the exact device is resolved.
```
