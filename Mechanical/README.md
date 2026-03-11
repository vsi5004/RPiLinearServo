# Mechanical — RPiLinearServo

3D-printable mounting hardware for the RPiLinearServo assembly. Multiple base variants are included to suit different installation orientations.

## Mount Variants

| File | Description |
|---|---|
| `Base - Vertical.3mf` | Standard vertical mount — actuator perpendicular to the mounting surface |
| `Base - Vertical (Long Screws).3mf` | Vertical mount variant with taller screw bosses for longer fasteners |
| `Base - Horizontal Left.3mf` | Horizontal mount — actuator parallel to the mounting surface, offset left |
| `Actuator Horn.3mf` | Output horn that attaches to the actuator slider |

The vertical mounts are intended for under-baseboard turnout installations where the actuator pushes/pulls a wire or rod through the surface. The horizontal mount is for side-mounting applications where the actuator drives a linkage parallel to the mounting plane.

## Print Settings

These parts are designed for FDM printing with standard settings:

| Parameter | Recommended |
|---|---|
| Layer height | 0.2 mm |
| Infill | 20–30 % |
| Material | PLA or PETG |
| Supports | Not required |
| Walls / perimeters | 3–4 |
| Top / bottom layers | 4–5 |

### Orientation

- **Base variants** — print flat on the mounting face (as oriented in the 3MF files). No supports needed.
- **Actuator horn** — print flat. The connection features are designed to print cleanly without supports.

## Additional Parts and Materials

Beyond the 3D-printed parts and the PCB assembly, you will need:

| Item | Qty | Notes |
|---|---|---|
| Micro stepper linear actuator (8.4 mm throw) | 1 | The actuator the PCB is designed around |
| M2 or M2.5 screws | 2–4 | For mounting the base to your surface (length depends on variant) |
| M2 screws (short) | 2 | For securing the PCB to the base mount |
| Hookup wire | — | For PWM signal and power connections |
| JB Weld Pro Cold Weld Epoxy| - | Used for fixing actuator horn to linear slide |
| 1.5mm OD x 1.0 ID x 20mm brass tubing | 1 | Acts as a bushing for pushrod | 
| Piano wire or control rod | 1 | For turnout linkage (vertical mount applications) |

## Assembly

1. Secure the PCB to the printed base using M2 screws.
2. Connect the stepper actuator to the J2 motor header.
3. Route the PWM signal wire and power leads.
4. If using a vertical mount for turnout control, run a piano wire or control rod through the horn and the baseboard to the tie bar.
5. Mount the assembly under the baseboard with M2/M2.5 screws.
