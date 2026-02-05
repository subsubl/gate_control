## 2024-10-26 - [Dark Mode Focus Indicators]
**Learning:** Default browser focus rings are often invisible on dark backgrounds, making keyboard navigation impossible.
**Action:** Always add high-contrast `:focus-visible` styles using the theme's accent color for dark mode interfaces.

## 2024-10-27 - [Sidebar Navigation Semantics]
**Learning:** This app uses `onclick` on `div` elements for sidebar navigation, which breaks keyboard accessibility.
**Action:** Convert sidebar `div`s to `<button>` elements and apply CSS resets (`background: none; border: none; width: 100%; text-align: left;`) to maintain the design while gaining native accessibility.

## 2024-10-27 - [Virtual Keypad Accessibility]
**Learning:** Virtual keypads relying solely on click handlers are inaccessible to keyboard users and lack native feel.
**Action:** Always implement a `keydown` listener that maps physical keys to the virtual buttons, triggering both the logic and the visual active state.

## 2026-02-05 - [Async Focus Management]
**Learning:** Disabling inputs during async operations (like login) prevents double-submission but can trap focus or prevent programmatic focusing if not re-enabled first.
**Action:** Always ensure elements are re-enabled before attempting to `.focus()` them in error handling or finalization blocks.
