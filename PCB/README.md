# PCB — RPiLinearServo

KiCad schematic and PCB design files for the RPiLinearServo controller board.

## GPIO Assignments

| Signal | RP2040-Zero | TMC2209 Pin | Notes |
|---|---|---|---|
| PWM input | GP0 | — | RC servo signal via PIO1/SM1 |
| nENABLE | GP1 | EN | Active-low driver enable |
| STEP | GP2 | STEP | PIO0/SM0 driven |
| DIR | GP3 | DIR | GPIO |
| UART TX | GP4 | PDN/UART | Via 1 kΩ series resistor on PCB |
| UART RX | GP5 | PDN/UART | Loopback on bus |
| INDEX | GP7 | INDEX | Unused (reserved) |
| DIAG | GP9 | DIAG | Unused (reserved) |
| LED | GP16 | — | WS2812 RGB via PIO1/SM0 |
| Hall sensor | GP26 | — | DRV5055 A2 analog via ADC0 |

### UART bus isolation

The 1 kΩ series resistor (R1) between GP4 and the TMC2209 PDN/UART pin provides signal isolation so the driver can pull the bus low during reply frames without fighting the RP2040 TX output. GP4 is tri-stated after each write to allow the TMC2209 to respond over the half-duplex bus.

### PWM input protection

The PWM input on GP0 is routed through a 74LVC1G17 Schmitt-trigger buffer (U4) for signal conditioning. A TPD1E10B06DPYR ESD protection diode (D1) clamps the input to protect against transient voltage spikes from the RC servo signal line.

## Bill of Materials

| Ref | Part | Value / Description | Package | Qty |
|---|---|---|---|---|
| U2 | Waveshare RP2040-Zero | Dual-core RP2040 MCU module | Through-hole module | 1 |
| U1 | BTT TMC2209 v1.3 SilentStepStick | Stepper driver with UART | Through-hole module | 1 |
| U3 | TI DRV5055A2xDBZxQ1 | Ratiometric linear hall-effect sensor, 25 mV/mT | SOT-23 | 1 |
| U4 | 74LVC1G17 | Single Schmitt-trigger buffer | SOT-23-5 | 1 |
| D1 | TI TPD1E10B06DPYR | ESD protection diode, ±30 kV HBM | 0402 | 1 |
| C1, C3 | Panasonic EEEFPC101XAP | 100 µF 16 V aluminum electrolytic | Radial (custom footprint) | 2 |
| C2, C5 | Ceramic | 2.2 µF | 0805 | 2 |
| C4, C6 | Ceramic | 0.1 µF (decoupling) | 0402 | 2 |
| R1, R2 | Resistor | 1 kΩ | 0603 | 2 |
| R3 | Resistor | 330 Ω | 0603 | 1 |
| J1 | Pin header 1×3 | Hall sensor / PWM input connector | 2.54 mm pitch | 1 |
| J2 | Pin header 2×2 (omit, used for pads only)| Stepper motor phase connector | 2.54 mm pitch | 1 |

### Sourcing notes

| Part | Suggested source |
|---|---|
| Waveshare RP2040-Zero | [Waveshare](https://www.waveshare.com/rp2040-zero.htm), AliExpress, Amazon |
| BTT TMC2209 v1.3 SilentStepStick | [BIGTREETECH](https://biqu.equipment/products/bigtreetech-tmc2209-stepper-motor-driver-for-3d-printer-board-vs-tmc2208), Amazon, AliExpress |
| TI DRV5055A2xDBZxQ1 | DigiKey, Mouser, LCSC |
| 74LVC1G17 | DigiKey, Mouser, LCSC |
| TPD1E10B06DPYR | DigiKey (296-30406-1-ND), Mouser |
| Panasonic EEEFPC101XAP | DigiKey (PCE4534CT-ND), Mouser |
| Passives (0402/0603/0805) | LCSC, DigiKey, Mouser |

All SMD passives use hand-solder footprints with extended pads for easier assembly.

## Design Files

| Path | Description |
|---|---|
| `RPiLinearServo.kicad_sch` | Schematic |
| `RPiLinearServo.kicad_pcb` | PCB layout |
| `RPiLinearServo.kicad_pro` | KiCad project |
| `RPiLinearServo.step` | 3D model of assembled board |
| `Components/` | Custom KiCad symbol libraries + 3D STEP models |
| `CustomFootprints/` | Custom KiCad footprint libraries |

## Manufacturing Outputs

Pre-generated Gerber and drill files are in `Outputs/`:

| File | Description |
|---|---|
| `*-F_Cu.gbr`, `*-B_Cu.gbr` | Front/back copper |
| `*-F_Mask.gbr`, `*-B_Mask.gbr` | Front/back solder mask |
| `*-F_Paste.gbr`, `*-B_Paste.gbr` | Front/back paste stencil |
| `*-F_Silkscreen.gbr`, `*-B_Silkscreen.gbr` | Front/back silkscreen |
| `*-Edge_Cuts.gbr` | Board outline |
| `*-PTH.drl`, `*-NPTH.drl` | Plated / non-plated drill files |
| `*-top-pos.csv`, `*-bottom-pos.csv` | Pick-and-place position files |
| `*-BOM.csv` | Bill of materials (semicolon-delimited) |
| `*-job.gbrjob` | Gerber job file |

These files are ready to upload directly to PCB fabricators such as JLCPCB, PCBWay, or OSH Park.

### PCB specifications

| Parameter | Value |
|---|---|
| Minimum trace width | 0.2 mm |
| Default trace width | 0.2 mm |
| Via diameter / drill | 0.6 mm / 0.3 mm |
| Copper-to-edge clearance | 0.4 mm |
| Layers | 2 |
