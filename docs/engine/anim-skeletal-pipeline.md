# Animation, skeletal and FBX mesh pipeline

## What it is

How animated and static meshes get from a DCC tool (Maya, 3ds Max, Blender) into Unreal 5.8, and how animation is driven there.
Import goes through Interchange: a translator reads the FBX (or glTF, USD, Alembic), and pipelines decide which assets to make. The main one is `UInterchangeGenericAssetsPipeline`, with mesh, animation, material and texture sub-pipelines.
Runtime animation runs in an Animation Blueprint, in Sequencer, or from live sources.
The static-mesh FBX rules near the end apply even without skeletons.

## Key concepts

- **Kinds of animation.**
  - Gameplay animation: input drives a pawn; its movement state feeds an Animation Blueprint, which picks and blends poses.
  - Linear animation: imported clips or keyframes played in Sequencer for cinematics or renders. In-engine keyframing uses Control Rig.
  - Live sources: Live Link streams mocap or a DCC into the editor; Take Recorder records it.
- **Root motion vs in place.**
  - Root motion: the clip's root-bone movement moves the character. Set `bEnableRootMotion` on the `UAnimSequence`, `RootMotionMode` on the anim instance, and the root lock (`ERootMotionRootLock`).
  - In place: the root stays still and the movement component moves the actor; the animation only matches its speed.
  - Root motion needs a real root bone at the origin.
- **Skeleton rules.**
  - One root bone at the origin. The pelvis (hip) is its child, not the root.
  - Hierarchy: pelvis → thighs and spine → clavicles and neck → head → face.
  - Bones that do not hang under the same root make a different skeleton.
  - Parent twist bones beside the limb chain, not in-line. Remove exporter-added leaf or end bones.
  - Meshes that share a `USkeleton` share animations. Different proportions need retargeting.
- **Skeletal mesh asset editors** (tabs of one editor family):
  - Skeleton: bone tree, sockets, virtual bones, anim notifies and curves.
  - Mesh: LODs, material slots, morph targets, skin weights.
  - Animation: sequences, blend spaces (`UBlendSpace`), montages (`UAnimMontage`), pose assets (`UPoseAsset`). Edit a sequence with FK Control Rig, or bake it to Control Rig.
  - Blueprint: the Animation Blueprint.
  - Physics: the `UPhysicsAsset` bodies and constraints.
- **Animation Blueprint.**
  - Event Graph: logic per update (Blueprint Update Animation, or `NativeUpdateAnimation` in a C++ `UAnimInstance`). It reads the owner and sets variables.
  - AnimGraph: the pose flow (state machines, blends, layers) that consumes those variables. Keep game logic out of it.
- **Sockets.**
  - A named transform on a bone (`USkeletalMeshSocket`) or on a static mesh.
  - Attach components to it and read its transform from code or Blueprint; it follows the bone.
- **Retargeting.**
  - Fab and marketplace animations usually come on another skeleton.
  - An IK Rig (`UIKRigDefinition`) per skeleton describes chains and goals. An IK Retargeter (`UIKRetargeter`) maps source to target and exports retargeted sequences.
- **Sequencer and Control Rig.**
  - Sequencer plays animation at runtime or for renders. It bakes to animation sequences and pose assets, and has event tracks and a curve editor.
  - Animation Mode tools: poses, tweens, snapper, motion trails, layers, pivots.
  - Control Rig (`UControlRig`, plugin `ControlRig`) adds rigs for keyframing in-engine.
  - The `SkeletalMeshModelingTools` plugin adds in-engine skinning and skeleton editing.
- **Interchange import of skeletal content.**
  - First import the mesh with its skeleton, animations off (`bImportAnimations`).
  - Then import clips animation-only (`bImportOnlyAnimations`, with the target skeleton set).
  - Clip range: `AnimationRange` (`EInterchangeAnimationRange`: Source Timeline, Animated Time, Set Range).
  - Sampling: `bUse30HzToBakeBoneAnimation`, or `CustomBoneAnimationSampleRate`.
  - Transform fixes: `ImportOffsetTranslation`, `ImportOffsetRotation`, `ImportOffsetUniformScale` on the assets pipeline.
  - FBX translator settings (`UInterchangeFbxTranslatorSettings`): `bConvertScene`, `bForceFrontXAxis`, `bConvertSceneUnit`.
  - Re-import reuses the settings stored on the asset (`UInterchangeAssetImportData`).
- **Static-mesh import options that matter.**
  - `bBakeMeshes`: bake the node hierarchy into the vertices. With it off, `bBakePivotMeshes` makes the DCC pivot the mesh origin.
  - `bImportLods`: import FBX LOD groups (needs bake meshes).
  - `CombineStaticMeshesBehavior`: one asset for the file, or one per node.
  - `bCollision`: import or generate collision. `bImportCollisionAccordingToMeshName`: read prefixed collision meshes.
  - `bBuildNanite`, `bGenerateLightmapUVs`, `bImportSockets`, `ForceAllMeshAsType`.

## Rules and pitfalls

- **Fix scale and axes in the DCC, not with import offsets.** Offsets and forced axes are a fallback; they make re-imports fragile.
- **Units.**
  - Unreal is 1 unit = 1 cm; Blender's default scene is 1 unit = 1 m.
  - Export from Blender with Apply Unit and the FBX units scale option, so a 1 m cube arrives as 100 cm. Or set the scene unit scale to 0.01.
  - Check one object of known size after the first import.
- **Axes.**
  - Unreal is Z-up with forward +X. Blender is Z-up, and models conventionally face −Y.
  - Either model facing +X and export forward X, up Z, or export forward −Y and let the importer convert. Pick one per project and keep it.
- **Blender FBX export settings.**
  - Apply Modifiers on; Add Leaf Bones off.
  - Smoothing "Normals Only" or Face, with sharp edges marked.
  - Bake Animation off for static meshes.
  - Selected objects only; mesh (and armature) types only.
  - Epic's Send to Unreal add-on automates this. It is an external Blender add-on, not part of the engine.
- **Maya and 3ds Max FBX export.**
  - Export the mesh and the skeleton's root joint together.
  - Animation on for clips; Deformed Models, Skins and Blend Shapes on.
  - Smoothing Groups on, Smooth Mesh off.
  - Complex non-skeletal deformation (cloth simulation, destruction) goes out as Alembic instead (plugin `AlembicImporter`).
- **Pivot.** With baked meshes the mesh origin is the DCC scene origin. Put the intended pivot (ground centre for buildings and units) at the DCC origin before export.
- **Collision prefixes.**
  - `UBX_` box, `UCP_` capsule, `USP_` sphere, `UCX_` convex, followed by the render mesh's name: `UCX_SM_Wall_00`.
  - Keep collision meshes simple and convex. Each `UCX_` mesh is one hull while `bOneConvexHullPerUCX` is on.
  - If the game gives actors their own collision, import none.
- **LODs.**
  - Either export an FBX LOD group (imported with `bImportLods`) or import LODs as separate files into LOD slots.
  - Set screen sizes deliberately, or use auto-computed ones (`bAutoComputeLODScreenSizes`).
- **Triangulate predictably.** FBX arrives triangulated. Triangulate in the DCC when shading or UV-baked data depends on the split.
- **Extra UV channels survive import, with V flipped.** Data written into UVs must be pre-flipped in the DCC.
- **One skeleton per proportion set.** Do not import a second near-identical skeleton. Point the import at the existing one, or retarget.
- **Re-import, do not delete and import.** Re-import keeps references, sockets and material assignments.
- **Keep sample rates consistent** across a clip set. Mixed rates cause foot sliding and blend artefacts.

## APIs, cvars and commands

| Name | Kind | Use |
|---|---|---|
| `UInterchangeManager::ImportAsset` | function | Scripted import with an overriding pipeline. |
| `UInterchangeGenericAssetsPipeline` | class | Top pipeline; holds the mesh, animation and material sub-pipelines. |
| `UInterchangeGenericMeshPipeline` | class | `bImportStaticMeshes`, `bImportSkeletalMeshes`, `bCollision`, `bImportCollisionAccordingToMeshName`, `bOneConvexHullPerUCX`, `bBuildNanite`, `bCreatePhysicsAsset`, `bImportMorphTargets`. |
| `UInterchangeGenericCommonMeshesProperties` | class | `ForceAllMeshAsType`, `bBakeMeshes`, `bBakePivotMeshes`, `bImportLods`, `bImportSockets`, `bRecomputeNormals`. |
| `UInterchangeGenericCommonSkeletalMeshesAndAnimationsProperties` | class | `bImportOnlyAnimations`, skeleton choice. |
| `UInterchangeGenericAnimationPipeline` | class | `bImportAnimations`, `AnimationRange`, `CustomBoneAnimationSampleRate`. |
| `UInterchangeFbxTranslatorSettings` | class | `bConvertScene`, `bForceFrontXAxis`, `bConvertSceneUnit`. |
| `UAnimInstance::NativeUpdateAnimation`, `BlueprintUpdateAnimation` | function | Per-update animation logic (C++ / Blueprint). |
| `UAnimSequence::bEnableRootMotion`, `ERootMotionMode`, `ERootMotionRootLock` | property / enum | Root motion setup. |
| `USkeletalMeshSocket` | class | Skeletal mesh socket. |
| `UIKRigDefinition`, `UIKRetargeter` | class | IK Rig and retargeting (plugin `IKRig`). |
| `UControlRig` | class | In-engine rigging (plugin `ControlRig`). |
| `UBlendSpace`, `UAnimMontage`, `UPoseAsset`, `UPhysicsAsset` | class | Animation and physics assets. |

## In World Divide

- The game has no skeletal meshes, Animation Blueprints or root motion today. Vehicle tracks move by material WPO driven from `UpdateRunningGear` through custom primitive data ([`../tech/scale/VEHICLE_TRACK_ANIMATION.md`](../tech/scale/VEHICLE_TRACK_ANIMATION.md)); turrets, wheels and recoil are transforms set in C++.
- Building and unit meshes come from Blender scripts in `Tools/Art/`. `usa_hq_blender.py` exports FBX with `axis_forward="X"`, `axis_up="Z"`, `apply_unit_scale`, `FBX_SCALE_UNITS`, `use_mesh_modifiers`, `add_leaf_bones=False`, `bake_anim=False`. The M20 track bake writes extra UV channels pre-flipped in V.
- Import is scripted, never by hand. `Tools/Editor/placeholder_art.py` and `vehicle_art.py` build an `InterchangeGenericAssetsPipeline` override (static mesh only, no collision, no Nanite, no materials, no animations); `setup_testmap.py` re-imports when a source changes. M20 LODs arrive as separate `_LOD<n>.fbx` files.
- Art candidates go under `/Game/Art/Candidates/` only ([`../ui/AI_TEAM.md`](../ui/AI_TEAM.md)). Mesh budgets: [`../tech/PERFORMANCE_GUIDELINES.md`](../tech/PERFORMANCE_GUIDELINES.md) rules 19–23 and 25. Project docs win on conflict.

## Sources

- Epic training course 107.01 *Introduction to Animation* (UE 5.5; paraphrased, with FBX import updated to Interchange for 5.8).
- Importing assets using Interchange: <https://dev.epicgames.com/documentation/en-us/unreal-engine/importing-assets-using-interchange-in-unreal-engine?application_version=5.8>
- FBX content pipeline: <https://dev.epicgames.com/documentation/en-us/unreal-engine/fbx-content-pipeline?application_version=5.8>
- IK Rig animation retargeting: <https://dev.epicgames.com/documentation/en-us/unreal-engine/ik-rig-animation-retargeting-in-unreal-engine?application_version=5.8>
- Skeletal mesh sockets: <https://dev.epicgames.com/documentation/en-us/unreal-engine/skeletal-mesh-sockets-in-unreal-engine?application_version=5.8>
- UE 5.8 engine source:
  - `Plugins/Interchange/Runtime/Source/Pipelines/Public/InterchangeGenericMeshPipeline.h`: collision prefixes, `bCollision`, `bOneConvexHullPerUCX`.
  - `.../Pipelines/Public/InterchangeGenericAssetsPipelineSharedSettings.h`, `InterchangeGenericAnimationPipeline.h`.
  - `Plugins/Interchange/Runtime/Source/Import/Public/Fbx/InterchangeFbxTranslator.h`.
  - `Runtime/Interchange/Engine/Public/InterchangeManager.h`, `InterchangeAssetImportData.h`.
  - `Runtime/Engine/Classes/Animation/AnimSequence.h`, `AnimInstance.h`, `AnimEnums.h`.
