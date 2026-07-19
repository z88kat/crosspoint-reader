#include "MappedInputManager.h"

#include <GfxRenderer.h>

#include "CrossPointSettings.h"

bool MappedInputManager::isNavDirectionSwapped() const {
  // Key the swap on the orientation the screen is *actually* rendered at, not the persisted reader
  // setting. The reader (and its modal menus) render rotated, so navigation/labels flip there; the
  // home and settings UI render in portrait, so they never flip even when a rotated reader is configured.
  const auto orientation = renderer.getOrientation();
  return SETTINGS.frontButtonFollowOrientation &&
         (orientation == GfxRenderer::PortraitInverted || orientation == GfxRenderer::LandscapeCounterClockwise);
}

bool MappedInputManager::mapButton(const Button button, bool (HalGPIO::*fn)(uint8_t) const) const {
  const auto sideLayout = SETTINGS.sideButtonLayout;

  switch (button) {
    case Button::Back:
      // Logical Back maps to user-configured front button.
      return (gpio.*fn)(SETTINGS.frontButtonBack);
    case Button::Confirm:
      // Logical Confirm maps to user-configured front button.
      return (gpio.*fn)(SETTINGS.frontButtonConfirm);
    case Button::Left:
      // Logical Left maps to user-configured front button.
      return (gpio.*fn)(SETTINGS.frontButtonLeft);
    case Button::Right:
      // Logical Right maps to user-configured front button.
      return (gpio.*fn)(SETTINGS.frontButtonRight);
    case Button::Up:
      // Side buttons remain fixed for Up/Down.
      return (gpio.*fn)(HalGPIO::BTN_UP);
    case Button::Down:
      // Side buttons remain fixed for Up/Down.
      return (gpio.*fn)(HalGPIO::BTN_DOWN);
    case Button::Power:
      // Power button bypasses remapping.
      return (gpio.*fn)(HalGPIO::BTN_POWER);
    case Button::PageBack:
      // Reader page navigation uses side buttons and can be swapped via settings.
      switch (sideLayout) {
        case CrossPointSettings::PREV_NEXT:
          return (gpio.*fn)(HalGPIO::BTN_UP);
        case CrossPointSettings::NEXT_PREV:
          return (gpio.*fn)(HalGPIO::BTN_DOWN);
        case CrossPointSettings::SIDE_BUTTONS_DISABLED:
        default:
          return false;
      }
    case Button::PageForward:
      // Reader page navigation uses side buttons and can be swapped via settings.
      switch (sideLayout) {
        case CrossPointSettings::PREV_NEXT:
          return (gpio.*fn)(HalGPIO::BTN_DOWN);
        case CrossPointSettings::NEXT_PREV:
          return (gpio.*fn)(HalGPIO::BTN_UP);
        case CrossPointSettings::SIDE_BUTTONS_DISABLED:
        default:
          return false;
      }
    case Button::NavNext:
      // Logical "next item" navigation: side Down + front Right, with the control axis flipped in
      // INVERTED / LANDSCAPE_CCW (frontButtonFollowOrientation) so it matches the rotated hint labels.
      return isNavDirectionSwapped() ? (mapButton(Button::Up, fn) || mapButton(Button::Left, fn))
                                     : (mapButton(Button::Down, fn) || mapButton(Button::Right, fn));
    case Button::NavPrevious:
      // Logical "previous item" navigation: side Up + front Left, axis-flipped in the same orientations.
      return isNavDirectionSwapped() ? (mapButton(Button::Down, fn) || mapButton(Button::Right, fn))
                                     : (mapButton(Button::Up, fn) || mapButton(Button::Left, fn));
  }

  return false;
}

void MappedInputManager::update() const {
  gpio.update();

  // Refresh per-frame touch state, then latch any tap that completed this frame.
  tapPending = false;
  synthButtonValid = false;

  float nx = 0.0f, ny = 0.0f;
  if (gpio.hasTouch() && gpio.wasTapped(nx, ny)) {
    renderer.tapToLogical(nx, ny, tapX, tapY);
    tapPending = true;

    // Synthesize a logical button from a tap in the on-screen bottom hint-bar
    // strip, so menu/list/home UIs are navigable without physical Back/Left/Right
    // (the M5Paper only has the 3-position wheel). The strip mirrors the 4-button
    // hint order drawn there, left-to-right: Back | Confirm | Left | Right. Taps
    // above the strip are positional-only (see wasTapped(), used by the reader).
    // NOTE: the strip height / quarter split is a first cut — tune it against the
    // rendered hint bar on hardware.
    const int w = renderer.getScreenWidth();
    const int h = renderer.getScreenHeight();
    if (w > 0 && h > 0 && tapY >= (h * 85) / 100) {
      const int quarter = (tapX * 4) / w;  // 0..3, left to right
      switch (quarter) {
        case 0:
          synthButton = Button::Back;
          break;
        case 1:
          synthButton = Button::Confirm;
          break;
        case 2:
          synthButton = Button::Left;
          break;
        default:
          synthButton = Button::Right;
          break;
      }
      synthButtonValid = true;
    }
  }
}

bool MappedInputManager::wasTapped(int& logicalX, int& logicalY) const {
  if (!tapPending) {
    return false;
  }
  logicalX = tapX;
  logicalY = tapY;
  return true;
}

bool MappedInputManager::touchSynthMatches(const Button button) const {
  if (!synthButtonValid) {
    return false;
  }
  if (button == synthButton) {
    return true;
  }
  // Let a synthesized Left/Right also satisfy the composed NavNext/NavPrevious
  // buttons, honoring the same direction swap mapButton() applies when the screen
  // is rendered rotated.
  const bool swapped = isNavDirectionSwapped();
  if (button == Button::NavNext) {
    return swapped ? (synthButton == Button::Left) : (synthButton == Button::Right);
  }
  if (button == Button::NavPrevious) {
    return swapped ? (synthButton == Button::Right) : (synthButton == Button::Left);
  }
  return false;
}

// A completed tap is a momentary gesture, so it satisfies the press EDGE
// (wasPressed) only — not the held level (isPressed) or release edge.
bool MappedInputManager::wasPressed(const Button button) const {
  return mapButton(button, &HalGPIO::wasPressed) || touchSynthMatches(button);
}

bool MappedInputManager::wasReleased(const Button button) const { return mapButton(button, &HalGPIO::wasReleased); }

bool MappedInputManager::isPressed(const Button button) const { return mapButton(button, &HalGPIO::isPressed); }

bool MappedInputManager::wasAnyPressed() const { return gpio.wasAnyPressed(); }

bool MappedInputManager::wasAnyReleased() const { return gpio.wasAnyReleased(); }

unsigned long MappedInputManager::getHeldTime() const { return gpio.getHeldTime(); }

MappedInputManager::Labels MappedInputManager::mapLabels(const char* back, const char* confirm, const char* previous,
                                                         const char* next) const {
  // Swap previous/next labels to match the page turn direction swap in INVERTED and LANDSCAPE_CCW.
  const bool swapLabels = isNavDirectionSwapped();
  const char* leftLabel = swapLabels ? next : previous;
  const char* rightLabel = swapLabels ? previous : next;

  // Build the label order based on the configured hardware mapping.
  auto labelForHardware = [&](uint8_t hw) -> const char* {
    // Compare against configured logical roles and return the matching label.
    if (hw == SETTINGS.frontButtonBack) {
      return back;
    }
    if (hw == SETTINGS.frontButtonConfirm) {
      return confirm;
    }
    if (hw == SETTINGS.frontButtonLeft) {
      return leftLabel;
    }
    if (hw == SETTINGS.frontButtonRight) {
      return rightLabel;
    }
    return "";
  };

  return {labelForHardware(HalGPIO::BTN_BACK), labelForHardware(HalGPIO::BTN_CONFIRM),
          labelForHardware(HalGPIO::BTN_LEFT), labelForHardware(HalGPIO::BTN_RIGHT)};
}

int MappedInputManager::getPressedFrontButton() const {
  // Scan the raw front buttons in hardware order.
  // This bypasses remapping so the remap activity can capture physical presses.
  if (gpio.wasPressed(HalGPIO::BTN_BACK)) {
    return HalGPIO::BTN_BACK;
  }
  if (gpio.wasPressed(HalGPIO::BTN_CONFIRM)) {
    return HalGPIO::BTN_CONFIRM;
  }
  if (gpio.wasPressed(HalGPIO::BTN_LEFT)) {
    return HalGPIO::BTN_LEFT;
  }
  if (gpio.wasPressed(HalGPIO::BTN_RIGHT)) {
    return HalGPIO::BTN_RIGHT;
  }
  return -1;
}
