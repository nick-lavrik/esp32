#include "EcoflowGridState.hpp"

const char *ecoflowGridStateName(EcoflowGridState state) {
  switch (state) {
    case EcoflowGridState::OnGrid: return "on-grid";
    case EcoflowGridState::OffGrid: return "off-grid";
    default: return "unknown";
  }
}
