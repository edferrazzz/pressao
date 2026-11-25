# PRESSÃO by audiomakers

Riser de pré-drop: um knob só que vai subindo o high pass e o reverb (e delay, ruído, estéreo, gate... conforme o preset)
até o drop. Quando o knob volta pro 0 no drop, o rabo do reverb/delay é cortado na hora e o drop entra seco.
A tela é um manômetro de panela de pressão: quanto mais riser, mais vapor; no topo a válvula apita; no drop ela solta tudo.

- **Um knob** (Pressão 0–100). Automatize subindo no pré-drop e volte pro 0 no drop.
- **10 presets** com setinhas (ou clique no nome): SUBIDA, FECHA E SOBE, CÉU, TÚNEL, ECO, VENTANIA, ASPIRADOR,
  TREMEDEIRA, RADINHO, FERVURA. A receita de cada um está em `Source/Presets.cpp`.
- **CRIAR**: monta o MEU PRESET escolhendo o teto de cada efeito (high pass, low pass, reverb, delay, estéreo, ruído,
  gate, gordura) e o tipo (filtro suave/ressonante, low pass fecha/fecha+sobe, reverb sala/hall/céu, delay 1/8 ponto/1/4/1/16,
  estéreo fecha/abre). O 100% do knob chega nesses tetos. Segurar um slider toca no 100% pra ouvir.
- Nível: compensa 70% da perda/ganho de volume do riser, com teto suave abaixo de 0 dBFS.
- Knob em 0 = som original.

## Código
- `Source/Presets.*` receitas dos presets (cada módulo entra numa faixa do knob)
- `Source/Engine.*` DSP: compressor → drive → bitcrush → clip (2x oversampling) → low/high pass → gate no tempo → ruído → estéreo → delay → reverb → nível
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
