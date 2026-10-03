#pragma once

// Підняти NetworkSupervisor і віддати йому радіо: профілі з NVS, засів з
// LittleFS і прошитого переліку (src/WifiNetworks.hpp), лог подій FSM.
// Неблокуючий - FSM крутиться у власному таску.
void setupNetworkSupervisor();
