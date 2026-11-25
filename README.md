# PRESSÃO by audiomakers

Plugin de pré-drop: um knob só que vai enchendo a pressão do som (satura, comprime, filtra, joga no reverb...)
até o drop. A tela é um manômetro de panela de pressão no fogo que esquenta, apita, treme e racha conforme o knob sobe.

- **Um knob** (Pressão 0–100). Automatize subindo no pré-drop e volte pro 0 no drop.
- **10 presets** com setinhas (ou clique no nome): SÓ PRESSÃO, FECHA E SOBE, TÚNEL, RADINHO, CÉU, ASPIRADOR,
  MOTOSERRA, TREMEDEIRA, ESTOURO, VULCÃO. A receita de cada um está em `Source/Presets.cpp`.
- Volume justo: o nível de saída acompanha o de entrada (sobe só uns 2 dB no máximo), com teto suave abaixo de 0 dBFS.
- Knob em 0 = som original.

## Código
- `Source/Presets.*` receitas dos presets (cada módulo entra numa faixa do knob)
- `Source/Engine.*` DSP: compressor → drive → bitcrush → clip (2x oversampling) → low/high pass → gate no tempo → mono → reverb → volume justo
- `Source/Processor.*` parâmetros (amount, preset, bypass); presets também aparecem como programas na DAW
- `Source/Editor.*` a tela animada (30 fps), tudo desenhado num só componente
- `Tests/Tests.cpp` checagens de áudio de todos os presets e posições do knob

## Build
```
cmake -S . -B build && cmake --build build --config Release --target Pressao_VST3 pressao_tests
ctest --test-dir build -C Release
```
Linux: `-DFETCHCONTENT_SOURCE_DIR_JUCE=<clone do JUCE 8.0.12>` evita baixar de novo.
`PRESSAO_SNAPSHOT_DIR=<pasta>` (com xvfb-run) grava PNGs da tela; `PRESSAO_DEMO_DIR=<pasta> PRESSAO_DEMO_INPUT=<wav> PRESSAO_DEMO_BPM=<bpm>`
grava um demo de pré-drop por preset.

O CI (GitHub Actions) compila no Windows, roda os testes, gera o instalador (Inno Setup) e commita em `Release/Windows/`.
