# Ragdoll preview engineering boundaries

PR #264 adds a read-only engineering preview for measured Fallout 4 ragdoll data. It does not add HKX authoring, serialized physics edits, body-to-body collision, or a production Havok solver.

The constrained drop is intentionally deterministic and uses fixed 1/120 second steps, measured body and constraint frames, captured pivot offsets, ground contact, and measured ragdoll or hinge limit clamps. Existing regression tests define this behavior and must remain unchanged unless a new proof demonstrates a defect.

## Ownership follow-up

`SkeletonView` currently owns both viewport interaction/rendering and the constrained-drop implementation. It is now too large for continued feature growth. Before adding more ragdoll simulation or engineering controls, split the drop state and constraint math into focused helpers and keep `SkeletonView` as the viewport/controller boundary. That split should be behavior-preserving and validated by the existing ragdoll regression and UI lifecycle suites.

Do not expand the preview into inferred Havok semantics. New layouts, constraint behavior, or serialization rules require measured fixture evidence first.
