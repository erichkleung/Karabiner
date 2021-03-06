#ifndef REMAPFUNCBASE_HPP
#define REMAPFUNCBASE_HPP

#include "bridge.h"
#include "FromEvent.hpp"
#include "IOLogWrapper.hpp"
#include "KeyCode.hpp"
#include "ListHookedKeyboard.hpp"
#include "RemapFuncClasses.hpp"
#include "ToEvent.hpp"

namespace org_pqrs_Karabiner {
namespace RemapFunc {
class RemapFuncBase {
protected:
  RemapFuncBase(unsigned int type) : type_(type), ignorePassThrough_(false) {}

public:
  virtual ~RemapFuncBase(void) {}

  virtual void add(AddDataType datatype, AddValue newval) = 0;

  virtual bool remap(RemapParams& remapParams) { return false; }
  virtual bool drop(const Params_KeyboardEventCallBack& params) { return false; }
  virtual bool remapSimultaneousKeyPresses(bool keyuponly) { return false; }
  virtual bool remapSetKeyboardType(KeyboardType& keyboardType) { return false; }
  virtual bool remapForceNumLockOn(ListHookedKeyboard::Item* item) { return false; }
  virtual const FromEvent* getBlockUntilKeyUpFromEvent(void) const { return NULL; }

  unsigned int getType(void) const { return type_; }
  void setIgnorePassThrough(bool v) { ignorePassThrough_ = v; }
  bool getIgnorePassThrough(void) const { return ignorePassThrough_; }

private:
  unsigned int type_;
  bool ignorePassThrough_;
};
}
}

#endif
