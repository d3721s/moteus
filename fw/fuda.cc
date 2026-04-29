#include "fw/fuda.h"

namespace moteus {

Fuda::Fuda(mjlib::micro::PersistentConfig* persistent_config) {
  persistent_config->Register("fuda", &config_, []() {});
}

}
