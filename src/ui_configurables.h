#ifndef UI_CONFIGURABLES_H_
#define UI_CONFIGURABLES_H_

#include "sensesp.h"
#include "sensesp/system/configurable.h"

using namespace sensesp;

/**
 * @brief Configurable for a single float value.
 *
 */
class FloatConfig : public Configurable {
 public:
  FloatConfig(float value, String config_path,
             String description, int sort_order = 1000)
      : value_(value),
        Configurable(config_path, description, sort_order) {
    load_configuration();
  }

  virtual void get_configuration(JsonObject& doc) override;
  virtual bool set_configuration(const JsonObject& config) override;
  virtual String get_config_schema() override;

  float get_value() { return value_; }

 protected:
  float value_ = 0.0;
};

/**
 * @brief Configurable for a single int value.
 *
 */
class IntConfig : public Configurable {
 public:
  IntConfig(int value, String config_path,
             String description, int sort_order = 1000)
      : value_(value),
        Configurable(config_path, description, sort_order) {
    load_configuration();
  }

  virtual void get_configuration(JsonObject& doc) override;
  virtual bool set_configuration(const JsonObject& config) override;
  virtual String get_config_schema() override;

  int get_value() { return value_; }

 protected:
  int value_ = 0;
};

#endif  // SH_WG_SRC_UI_CONTROLS_H_
