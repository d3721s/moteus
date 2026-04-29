#pragma once

#include "mjlib/base/visitor.h"
#include "mjlib/micro/persistent_config.h"

namespace moteus {

struct Fuda {
  explicit Fuda(mjlib::micro::PersistentConfig* persistent_config);

  struct Config {
    bool enable = false;

    template <typename Archive>
    void Serialize(Archive* a) {
      a->Visit(MJ_NVP(enable));
    }
  };

  Config* config() { return &config_; }
  const Config* config() const { return &config_; }

  template <typename Archive>
  void Serialize(Archive* a) {
    (void)a;
  }

 private:
  Config config_;
};

}
