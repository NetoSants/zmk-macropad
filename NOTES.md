# Macropad ZMK Context

## Goal
Firmware ZMK funcional para um macropad de 15 teclas + 3 encoders EC11, handwired, BLE, com Nice!Nano (nRF52840).

## Hardware
- Placa: Nice!Nano v2
- Encoder 1: P1.13 (A) / P1.15 (B)
- Matriz 1×1 (teste): col=gpio0.22, row=gpio0.6
- LED azul P0.15 (BLE status)

## Build System
- Repositório de config ZMK (build-user-config)
- ZMK v0.3 / Zephyr 3.5.0
- Build via GitHub Actions (`zmkfirmware/zmk/.github/workflows/build-user-config.yml@v0.3`)
- `ZMK_EXTRA_MODULES` aponta pra raiz do config repo
- Shield: `macropad`, Board: `nice_nano`

## Estrutura Atual do Módulo Custom
```
/
├── CMakeLists.txt              → add_subdirectory(src)
├── Kconfig                     → CUSTOM_ENCODER bool, default y
├── src/
│   ├── CMakeLists.txt          → zephyr_library() + sources
│   ├── encoder_custom.c        → driver custom EC11
│   └── encoder_custom.h        → defines ENC_A_PIN=13, ENC_B_PIN=15
├── zephyr/
│   └── module.yml              → cmake: ., kconfig: Kconfig, board_root: .
├── boards/shields/macropad/
│   ├── macropad.overlay        → kscan only (sem encoder nodes)
│   ├── macropad.keymap         → bindings = <&none>
│   ├── macropad.conf           → ZMK_BLE=y, ZMK_USB=y, sem CONFIG_EC11
│   ├── Kconfig.shield
│   └── Kconfig.defconfig
└── config/west.yml             → import: app/west.yml @v0.3
```

## Driver Encoder
- `encoder_custom.c`: GPIO ISR em P1.13/P1.15 + lookup-table + workqueue
- Envia Seta Cima (0x00070052) ou Seta Baixo (0x00070051) via zmk_hid_press/release
- Chama `zmk_endpoints_send_report(HID_USAGE_KEY)` após cada press/release
- Inclui teste automático: digita "A" 3s após boot

## Progresso (22/05/2026 - Sessão Final)

### Root Cause #1: `CONFIG_EC11_TRIGGER_GLOBAL_THREAD` faltando
- Kconfig do EC11 default: `EC11_TRIGGER_NONE` → driver compila SEM interrupção de GPIO
- Sem interrupção, o driver fica inerte (polling manual nunca chamado)
- Build #20: adicionado `CONFIG_EC11_TRIGGER_GLOBAL_THREAD=y`
- **Ainda não funcionou** — havia segundo problema

### Root Cause #2: `steps=60` com default `triggers_per_rotation=20` → 0 triggers
- Behavior `sensor_rotate_common.c` (v0.3):
  - `trigger_degrees = 360 / triggers_per_rotation` = 360/20 = 18
  - Cada pulso reporta `val1 = (1 * 360) / steps` = 360/60 = 6 graus
  - `triggers = 6 / 18 = 0` (divisão inteira!) — **zero disparos por pulso**

### Fix aplicado no overlay:
- `steps = <120>` (30 detentes × 4 transições quadrature por detente)
- `triggers-per-rotation = <30>` (1 trigger por detente)
- A cada 4 pulsos: remainder acumula 12°, `triggers = 12/12 = 1` ✓

### Lições Aprendidas
- Legacy mode (`steps=0`, `resolution=4`) tem bug no v0.3: `val1 = ticks ≠ 0`, então `if(val1==0)` shim NÃO funciona
- Único modo funcional: `steps` + `triggers-per-rotation` com a matemática correta
- `clk-gpios`/`dt-gpios` → `a-gpios`/`b-gpios` (binding `alps,ec11`)
- `GPIO_INT_EDGE_BOTH` + `setup_int(dev,false)` mascara ambos os pinos no primeiro ISR
- Para testar: flashar firmware, rotacionar encoder devagar, verificar se USB envia UP/DOWN

### 1. Macro HID errada (build #14)
- Código antigo usava `HID_USAGE_KEY_KEYBOARD_UP` (não existe no ZMK v0.3)
- Correto: `HID_USAGE_KEY_KEYBOARD_UPARROW` e `HID_USAGE_KEY_KEYBOARD_DOWNARROW`
- Defines em `dt-bindings/zmk/hid_usage.h`

### 2. Faltava zmk_endpoints_send_report (build #14)
- `zmk_hid_press()` só seta bit no buffer — não transmite
- Necessário `zmk_endpoints_send_report(HID_USAGE_KEY)` para enviar report USB/BLE
- Assinatura: `int zmk_endpoints_send_report(uint16_t usage_page)`

### 3. module.yml cmake key errada (build #16 failed)
- `cmake: CMakeLists.txt` → erro: "folder value which does not contain a CMakeLists.txt file"
- `cmake` espera um **diretório**, não o arquivo
- Corrigido: `cmake: .`

### 4. zephyr_library() include paths
- `zephyr_library()` cria target que herda include paths do `zephyr_interface`
- Zephyr adiciona `APPLICATION_SOURCE_DIR/include` globalmente via boilerplate
- Portanto `<zmk/hid.h>` é encontrado mesmo em bibliotecas externas

### 5. Referências da API ZMK
- `zmk_hid_press(uint32_t usage)` → page<<16 | id
- `zmk_hid_release(uint32_t usage)` → mesma lógica
- `zmk_endpoints_send_report(uint16_t usage_page)` → HID_USAGE_KEY (0x07) ou HID_USAGE_CONSUMER (0x0C)
- `zmk_hid_keyboard_press(zmk_key_t key)` → só o usage ID (ex: 0x04 = A)
- `zmk_hid_keyboard_release(zmk_key_t key)`
- Todas declaradas em `<zmk/hid.h>` e `<zmk/endpoints.h>`

## Hardware Verificado
- Encoder funciona no Arduino (sketch `encoder_test/`) com `INPUT_PULLUP`
- Pinos 45 (P1.13) e 47 (P1.15) OK com lookup-table
- USB detecta o teclado como "dispositivo de teclado HID"

## Pendências
- [ ] Build #17 (b7dc815) — verificar se compila com module.yml corrigido
- [ ] Testar encoder em USB após build bem-sucedido
- [ ] Se encoder funcionar, trocar setas para C_VOL_UP/C_VOL_DN
- [ ] Se não funcionar, testar com `alps,ec11` stock + CONFIG_EC11=y
- [ ] Adicionar mais 2 encoders e matriz 14 teclas
