#include "ui_configurables.h"

static const char kFloatConfigSchema[] = R"({
    "type": "object",
    "properties": {
        "value": { "title": "value", "type": "string" }
    }
  })";

String FloatConfig::get_config_schema() { return kFloatConfigSchema; }

void FloatConfig::get_configuration(JsonObject& root) {
  root["value"] = (float)value_;
}

bool FloatConfig::set_configuration(const JsonObject& config) {
  if (!config.containsKey("value")) {
    return false;
  } else {
    value_ = (float)config["value"];
  }

  return true;
}


static const char kIntConfigSchema[] = R"({
    "type": "object",
    "properties": {
        "value": { "title": "value", "type": "integer" }
    }
  })";

String IntConfig::get_config_schema() { return kIntConfigSchema; }

void IntConfig::get_configuration(JsonObject& root) {
  root["value"] = value_;
}

bool IntConfig::set_configuration(const JsonObject& config) {
  if (!config.containsKey("value")) {
    return false;
  } else {
    value_ = config["value"];
  }

  return true;
}