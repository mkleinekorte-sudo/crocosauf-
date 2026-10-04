#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p tests/build work_tests
c++ -std=c++17 -Wall -Wextra -Werror -I tests/include tests/regression.cpp -o tests/build/regression
c++ -std=c++17 -Wall -Wextra -Werror -I tests/include tests/bluetooth.cpp -o tests/build/bluetooth
c++ -std=c++17 -Wall -Wextra -Werror -I tests/include tests/sound_pinchen.cpp -o tests/build/sound_pinchen
for scenario in welcome welcome_missing welcome_stuck normal ten_rounds switch_phases tests_interrupt wifi_idle reminder_sleep timer_wake wake_interrupted mute_volume audio_keepalive pickers web_export animations wrap; do
    tests/build/regression "$scenario"
done
for scenario in approval settings framing lifecycle; do tests/build/bluetooth "$scenario"; done
for scenario in welcome_selection welcome_reboot_sequence gameover_sequence gameover_random pinchen_pairs_shuffle pinchen_resume audio_spacing; do
    tests/build/sound_pinchen "$scenario"
done
java com.sun.tools.javac.Main -d tests/build android/app/src/main/java/de/mkrativ/crocosauf/WireProtocol.java tests/java/ProtocolTest.java tests/java/ParseSources.java
java -cp tests/build ProtocolTest
java -cp tests/build ParseSources android/app/src/main/java/de/mkrativ/crocosauf/MainActivity.java android/app/src/main/java/de/mkrativ/crocosauf/BleClient.java android/app/src/main/java/de/mkrativ/crocosauf/WireProtocol.java
