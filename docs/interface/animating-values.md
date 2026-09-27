# Animating a value

opane can move a custom shader's uniform to a new value over time instead of
setting it at once. Combined with a [custom shader](../extending/custom-shaders.md),
this is how you animate effects such as a glow that fades in when the pointer
arrives.

```cpp
button->OnHoverStart = [&] { button->AnimateUniform("Glow", 1.0f, 0.0f, 0.0f, 0.0f, 0.2f); };
button->OnHoverEnd   = [&] { button->AnimateUniform("Glow", 0.0f, 0.0f, 0.0f, 0.0f, 0.2f); };

app.AnimateMaterialUniform(aurora, "Tint", opane::Color::FromBytes(255, 190, 120), 0.4f,
                           opane::Easing::Smooth);
```

- `Element::AnimateUniform` animates a uniform of that element's own material.
- `App::AnimateMaterialUniform` animates a uniform of any material.
- `App::CancelMaterialAnimations(material)` stops every animation on a
  material, leaving the values where they are.

Animating a uniform that is already moving replaces the animation in flight,
starting from the value it had reached. So a hover that is entered, left, and
entered again moves smoothly and never jumps.

## Easing

| `Easing` | Movement |
|---|---|
| `Smooth` (default) | Starts and ends gently. |
| `Linear` | Constant speed. |
| `In` | Starts slowly and speeds up. |
| `Out` | Starts quickly and slows down. |

## Animating looks and layout

Changes of state (hovered, pressed, and so on) are animated for you when an
element has an `Appearance`; `Transition` and `Curve` on its `Style` set how
long they take and their easing. See
[Designing your own look](../styling/your-own-look.md).

Other properties, such as `Transform`, `Opacity`, or a slider's value, are not
animated by opane. Change them yourself each frame in a frame function or in
an element's `OnUpdate`.

## Limitations

- Only material uniforms have built-in animation. There is no general tween
  system for arbitrary properties.
- There are no keyframe timelines or spring animations.
