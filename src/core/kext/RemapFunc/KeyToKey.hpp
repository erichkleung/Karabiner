#ifndef KEYTOKEY_HPP
#define KEYTOKEY_HPP

#include "RemapFuncBase.hpp"

namespace org_pqrs_Karabiner {
namespace RemapFunc {
class KeyToKey final : public RemapFuncBase {
public:
  KeyToKey(void) : RemapFuncBase(BRIDGE_REMAPTYPE_KEYTOKEY),
                   index_(0),
                   currentToEvent_(CurrentToEvent::TOKEYS),
                   keyboardRepeatID_(-1),
                   isRepeatEnabled_(true),
                   delayUntilRepeat_(-1),
                   keyRepeat_(-1) {}

  bool remap(RemapParams& remapParams) override;

  // ----------------------------------------
  // [0] => fromEvent_
  // [1] => toKeys_[0]
  // [2] => toKeys_[1]
  // [3] => ...
  void add(AddDataType datatype, AddValue newval) override;

  // ----------------------------------------
  // utility functions
  void add(KeyCode newval) { add(AddDataType(BRIDGE_DATATYPE_KEYCODE), AddValue(newval.get())); }
  void add(Option newval) { add(AddDataType(BRIDGE_DATATYPE_OPTION), AddValue(newval.get())); }

  bool call_remap_with_VK_PSEUDO_KEY(EventType eventType);

  size_t toKeysSize(void) const { return toKeys_.size(); }
  void clearToKeys(void);

private:
  int getDelayUntilRepeat(void);
  int getKeyRepeat(void);

  class CurrentToEvent final {
  public:
    enum Value {
      TOKEYS,
      BEFOREKEYS,
      AFTERKEYS,
    };
  };

  Vector_ToEvent& getCurrentToEvent(void) {
    switch (currentToEvent_) {
    case CurrentToEvent::TOKEYS:
      return toKeys_;
    case CurrentToEvent::BEFOREKEYS:
      return beforeKeys_;
    case CurrentToEvent::AFTERKEYS:
      return afterKeys_;
    }
  }

  size_t index_;

  FromEvent fromEvent_;
  Vector_ModifierFlag fromModifierFlags_;
  Vector_ModifierFlag pureFromModifierFlags_; // fromModifierFlags_ - fromEvent_.getModifierFlag()

  Vector_ToEvent toKeys_;
  Vector_ToEvent beforeKeys_;
  Vector_ToEvent afterKeys_;
  CurrentToEvent::Value currentToEvent_;

  int keyboardRepeatID_;
  bool isRepeatEnabled_;

  int delayUntilRepeat_;
  int keyRepeat_;
};
DECLARE_VECTOR(KeyToKey);
}
}

#endif
