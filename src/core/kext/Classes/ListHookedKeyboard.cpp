#define protected public // A hack for access private member of IOHIKeyboard
#define private public
#include <IOKit/hidsystem/IOHIKeyboard.h>
#undef protected
#undef private

#include "CommonData.hpp"
#include "Config.hpp"
#include "EventInputQueue.hpp"
#include "FlagStatus.hpp"
#include "GlobalLock.hpp"
#include "IOLogWrapper.hpp"
#include "ListHookedKeyboard.hpp"
#include "RemapClass.hpp"

namespace org_pqrs_Karabiner {
namespace {
ListHookedKeyboard listHookedKeyboard;
}
TimerWrapper ListHookedKeyboard::setcapslock_timer_;

void
ListHookedKeyboard::static_initialize(IOWorkLoop& workloop) {
  setcapslock_timer_.initialize(&workloop, NULL, ListHookedKeyboard::setcapslock_timer_callback);
}

void
ListHookedKeyboard::static_terminate(void) {
  setcapslock_timer_.terminate();
}

ListHookedKeyboard&
ListHookedKeyboard::instance(void) {
  return listHookedKeyboard;
}

ListHookedKeyboard::Item::Item(IOHIDevice* p) : ListHookedDevice::Item(p),
                                                orig_keyboardEventAction_(NULL),
                                                orig_keyboardEventTarget_(NULL),
                                                orig_updateEventFlagsAction_(NULL),
                                                orig_updateEventFlagsTarget_(NULL) {}

ListHookedKeyboard::Item::~Item(void) {
  IOLOG_DEBUG("ListHookedKeyboard::Item::~Item()\n");
  restoreEventAction();
}

// ======================================================================
bool
ListHookedKeyboard::Item::refresh(void) {
  if (!device_) goto restore;

  {
    const char* name = device_->getName();
    if (!name) goto restore;

    if (ListHookedDevice::Item::isConsumer(name)) goto restore;
  }

  // ------------------------------------------------------------
  if (!Config::get_initialized()) {
    goto restore;
  }
  if (!Config::get_essential_config(BRIDGE_ESSENTIAL_CONFIG_INDEX_notsave_automatically_enable_keyboard_device)) {
    goto restore;
  }
  if (Config::get_essential_config(BRIDGE_ESSENTIAL_CONFIG_INDEX_general_dont_remap_thirdvendor_keyboard) &&
      deviceType_ != DeviceType::APPLE_INTERNAL &&
      deviceType_ != DeviceType::APPLE_EXTERNAL) {
    goto restore;
  }
  if (Config::get_essential_config(BRIDGE_ESSENTIAL_CONFIG_INDEX_general_dont_remap_internal) &&
      deviceType_ == DeviceType::APPLE_INTERNAL) {
    goto restore;
  }
  if (Config::get_essential_config(BRIDGE_ESSENTIAL_CONFIG_INDEX_general_dont_remap_external) &&
      deviceType_ != DeviceType::APPLE_INTERNAL) {
    goto restore;
  }
  if (Config::get_essential_config(BRIDGE_ESSENTIAL_CONFIG_INDEX_general_dont_remap_apple_keyboard) &&
      getDeviceIdentifier().isEqualVendor(DeviceVendor::APPLE_COMPUTER)) {
    goto restore;
  }
  if (Config::get_essential_config(BRIDGE_ESSENTIAL_CONFIG_INDEX_general_dont_remap_non_apple_keyboard) &&
      !getDeviceIdentifier().isEqualVendor(DeviceVendor::APPLE_COMPUTER)) {
    goto restore;
  }

  // Logitech USB Headset
  if (getDeviceIdentifier().isEqualVendorProduct(DeviceVendor::LOGITECH, DeviceProduct::LOGITECH_USB_HEADSET)) {
    goto restore;
  }
  // Kensington Virtual Device (0x0, 0x0)
  if (getDeviceIdentifier().isEqualVendorProduct(DeviceVendor::PSEUDO, DeviceProduct::PSEUDO)) {
    if (Config::get_essential_config(BRIDGE_ESSENTIAL_CONFIG_INDEX_general_allow_devices_vendor_id_product_id_are_zero)) {
      // do nothing
    } else {
      // Note: USB Overdrive also use 0x0,0x0.
      // We allow to use USB Overdrive.
      if (deviceType_ != DeviceType::USB_OVERDRIVE) {
        goto restore;
      }
    }
  }

  // ------------------------------------------------------------
  return replaceEventAction();

restore:
  return restoreEventAction();
}

bool
ListHookedKeyboard::Item::replaceEventAction(void) {
  if (!device_) return false;

  IOHIKeyboard* kbd = OSDynamicCast(IOHIKeyboard, device_);
  if (!kbd) return false;

  bool result = false;

  // ------------------------------------------------------------
  {
    // Logitech's driver (LCC) modifies _keyboardEventAction after we changed it.
    // So, we need to replace _keyboardEventAction until it points Karabiner's callback function.
    KeyboardEventCallback callback = reinterpret_cast<KeyboardEventCallback>(kbd->_keyboardEventAction);

    if (!callback) {
      IOLOG_DEBUG("HookedKeyboard::replaceEventAction (KeyboardEventCallback) device_:%p callback is null.\n", device_);
      inProgress_ = true;

    } else if (callback != EventInputQueue::push_KeyboardEventCallback) {
      inProgress_ = false;

      IOLOG_DEBUG("HookedKeyboard::replaceEventAction (KeyboardEventCallback) device_:%p (%p -> %p)\n",
                  device_, callback, EventInputQueue::push_KeyboardEventCallback);

      orig_keyboardEventAction_ = callback;
      orig_keyboardEventTarget_ = kbd->_keyboardEventTarget;

      kbd->_keyboardEventAction = reinterpret_cast<KeyboardEventAction>(EventInputQueue::push_KeyboardEventCallback);

      result = true;
    }
  }
  {
    // We need to replace _updateEventFlagsAction until it points Karabiner's callback function.
    // (A reason is described at ListHookedKeyboard::replaceEventAction.)
    UpdateEventFlagsCallback callback = reinterpret_cast<UpdateEventFlagsCallback>(kbd->_updateEventFlagsAction);
    if (!callback) {
      IOLOG_DEBUG("HookedKeyboard::replaceEventAction (UpdateEventFlagsCallback) device_:%p callback is null.\n", device_);
      inProgress_ = true;

    } else if (callback != EventInputQueue::push_UpdateEventFlagsCallback) {
      inProgress_ = false;

      IOLOG_DEBUG("HookedKeyboard::replaceEventAction (UpdateEventFlagsCallback) device_:%p (%p -> %p)\n",
                  device_, callback, EventInputQueue::push_UpdateEventFlagsCallback);

      orig_updateEventFlagsAction_ = callback;
      orig_updateEventFlagsTarget_ = kbd->_updateEventFlagsTarget;

      kbd->_updateEventFlagsAction = reinterpret_cast<UpdateEventFlagsAction>(EventInputQueue::push_UpdateEventFlagsCallback);

      result = true;
    }
  }

  return result;
}

bool
ListHookedKeyboard::Item::restoreEventAction(void) {
  if (!device_) return false;

  IOHIKeyboard* kbd = OSDynamicCast(IOHIKeyboard, device_);
  if (!kbd) return false;

  bool result = false;

  // ----------------------------------------
  {
    KeyboardEventCallback callback = reinterpret_cast<KeyboardEventCallback>(kbd->_keyboardEventAction);
    if (callback == EventInputQueue::push_KeyboardEventCallback) {
      IOLOG_DEBUG("HookedKeyboard::restoreEventAction (KeyboardEventCallback) device_:%p (%p -> %p)\n",
                  device_, callback, orig_keyboardEventAction_);

      kbd->_keyboardEventAction = reinterpret_cast<KeyboardEventAction>(orig_keyboardEventAction_);

      result = true;
    }
  }
  {
    UpdateEventFlagsCallback callback = reinterpret_cast<UpdateEventFlagsCallback>(kbd->_updateEventFlagsAction);
    if (callback == EventInputQueue::push_UpdateEventFlagsCallback) {
      IOLOG_DEBUG("HookedKeyboard::restoreEventAction (UpdateEventFlagsCallback) device_:%p (%p -> %p)\n",
                  device_, callback, orig_updateEventFlagsAction_);

      kbd->_updateEventFlagsAction = reinterpret_cast<UpdateEventFlagsAction>(orig_updateEventFlagsAction_);

      result = true;
    }
  }

  orig_keyboardEventAction_ = NULL;
  orig_keyboardEventTarget_ = NULL;
  orig_updateEventFlagsAction_ = NULL;
  orig_updateEventFlagsTarget_ = NULL;

  return result;
}

// ======================================================================
void
ListHookedKeyboard::Item::apply(const Params_KeyboardEventCallBack& params) {
  if (params.key >= KeyCode::VK__BEGIN__) {
    // Invalid keycode
    IOLOG_ERROR("ListHookedKeyboard::Item::apply invalid key:%d eventType:%d\n", params.key.get(), params.eventType.get());
    return;
  }
  if (params.eventType == EventType::MODIFY && !params.key.isModifier()) {
    // Invalid modifierkeycode
    IOLOG_ERROR("ListHookedKeyboard::Item::apply invalid modifierkey:%08x\n", params.key.get());
    return;
  }

  // ------------------------------------------------------------
  if (RemapClassManager::remap_dropkeyafterremap(params)) return;

  // ------------------------------------------------------------
  KeyboardEventCallback callback = orig_keyboardEventAction_;
  if (!callback) return;

  OSObject* target = orig_keyboardEventTarget_;
  if (!target) return;

  OSObject* sender = OSDynamicCast(OSObject, device_);
  if (!sender) return;

  const AbsoluteTime& ts = CommonData::getcurrent_ts();
  OSObject* refcon = NULL;

  // ----------------------------------------
  // Send an UpdateEventFlags event if needed. (Send before KeyboardEvent when EventType::DOWN)
  bool needUpdateEventFlagsEvent = false;
  if (params.flags.isOn(ModifierFlag::NUMPAD)) {
    needUpdateEventFlagsEvent = true;
  }

  if (needUpdateEventFlagsEvent && params.eventType == EventType::DOWN) {
    Params_UpdateEventFlagsCallback p(params.flags);
    apply(p);
  }

  // ----------------------------------------
  Params_KeyboardEventCallBack::log(false, params.eventType, params.flags, params.key, params.keyboardType, params.repeat);
  {
    // We need to unlock the global lock while we are calling the callback function.
    //
    // When Sticky Keys (in Universal Access) is activated,
    // Apple driver calls EventInputQueue::push_* recursively.
    //
    // If we don't unlock the global lock, deadlock is occured.
    GlobalLock::ScopedUnlock lk;
    callback(target, params.eventType.get(), params.flags.get(), params.key.get(),
             params.charCode.get(), params.charSet.get(), params.origCharCode.get(), params.origCharSet.get(),
             params.keyboardType.get(), params.repeat, ts, sender, refcon);
  }

  // ----------------------------------------
  // Send an UpdateEventFlags event if needed. (Send after KeyboardEvent when EventType::UP)
  if (needUpdateEventFlagsEvent && params.eventType == EventType::UP) {
    Flags stripped(params.flags);
    stripped.stripNUMPAD();

    Params_UpdateEventFlagsCallback p(stripped);
    apply(p);
  }

  // ----------------------------------------
  // The CapsLock LED is not designed to turn it on/off frequently.
  // So, we have to use the timer to call a setAlphaLock function at appropriate frequency.
  enum {
    CAPSLOCK_LED_DELAY_MS = 5,
  };
  setcapslock_timer_.setTimeoutMS(CAPSLOCK_LED_DELAY_MS, false);
}

void
ListHookedKeyboard::Item::apply(const Params_UpdateEventFlagsCallback& params) {
  // ------------------------------------------------------------
  UpdateEventFlagsCallback callback = orig_updateEventFlagsAction_;
  if (!callback) return;

  OSObject* target = orig_updateEventFlagsTarget_;
  if (!target) return;

  OSObject* sender = OSDynamicCast(OSObject, device_);
  if (!sender) return;

  OSObject* refcon = NULL;

  Params_UpdateEventFlagsCallback::log(false, params.flags);
  {
    // We need to unlock the global lock while we are calling the callback function.
    // For more information, See ListHookedKeyboard::Item::apply(const Params_KeyboardEventCallBack& params)
    GlobalLock::ScopedUnlock lk;
    callback(target, params.flags.get(), sender, refcon);
  }
}

void
ListHookedKeyboard::apply(const Params_KeyboardEventCallBack& params) {
  ListHookedKeyboard::Item* p = static_cast<ListHookedKeyboard::Item*>(get_replaced());
  if (p) {
    p->apply(params);
  }
}

void
ListHookedKeyboard::apply(const Params_UpdateEventFlagsCallback& params) {
  ListHookedKeyboard::Item* p = static_cast<ListHookedKeyboard::Item*>(get_replaced());
  if (p) {
    p->apply(params);
  }
}

void
ListHookedKeyboard::setcapslock_timer_callback(OSObject* owner, IOTimerEventSource* sender) {
  ListHookedKeyboard& self = ListHookedKeyboard::instance();

  if (Config::get_essential_config(BRIDGE_ESSENTIAL_CONFIG_INDEX_general_passthrough_capslock_led_status)) return;

  Flags flags = FlagStatus::globalFlagStatus().makeFlags();

  for (Item* p = static_cast<Item*>(self.list_.safe_front()); p; p = static_cast<Item*>(p->getnext())) {
    if (!p->isReplaced()) continue;

    // Don't call setAlphaLock on devices which have non-Apple driver.
    if (p->getDeviceType() != DeviceType::APPLE_INTERNAL &&
        p->getDeviceType() != DeviceType::APPLE_EXTERNAL) {
      continue;
    }

    IOHIKeyboard* kbd = OSDynamicCast(IOHIKeyboard, p->get());
    if (!kbd) continue;

    {
      GlobalLock::ScopedUnlock lk;

      // We call setAlphaLock to match a state of CapsLock of the hardware with remapped CapsLock.
      if (flags.isOn(ModifierFlag::CAPSLOCK)) {
        if (!kbd->alphaLock()) {
          kbd->setAlphaLock(true);
        }
      } else {
        if (kbd->alphaLock()) {
          kbd->setAlphaLock(false);
        }
      }
    }
  }
}
}
