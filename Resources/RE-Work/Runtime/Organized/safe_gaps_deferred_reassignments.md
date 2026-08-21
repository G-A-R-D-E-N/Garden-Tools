# Deferred: 625 safe-gap fills with a CLASS REASSIGNMENT (2026-07-10)

These are the subset of the 2,803 `crossref_safe_naming_gaps` entries where the 1.10.155-decoded name attributes the vtable slot to a DIFFERENT class than the CommonLib placeholder's class (e.g. `MovementControllerNPC::Func5` -> `MovementControllerAI::<method>`). Filling a placeholder with a *differently-classed* name is the exact base-vs-derived / sibling-class ambiguity that produced all 13 reverts in the misname pass (see 07-DECOMPILE-CANDIDATES.md), so these were deliberately HELD OUT of the safe-gap auto-apply and are recorded here for a decompile/header-verified follow-up instead.

- **625 total deferred.**
  - **423** are `std::_Func_impl`/`_Func_base` STL functor template internals -- the "reassignment" is really just template-name formatting (`std::_Func_base_void_...` sanitized vs the demangled `std::_Func_base<void,...>`); zero RE value, safe to ignore.
  - **202** are genuine engine-class reassignments worth reviewing (base-vs-derived attribution: the vtable slot likely points at an inherited base-class implementation). These are the meaningful follow-up.

## Engine-class reassignments to review (decompile each before applying)

| Address | Placeholder class::name | Decoded (reassigned) name |
|---|---|---|
| `0x1428C83D0` | `BSImagespaceShaderVatsTargetDebug::Func3` | `BSImagespaceShader::SetupTechnique` |
| `0x142825E70` | `BSLightingShaderMaterial::Func4` | `BSLightingShaderMaterialBase::ComputeCRC32` |
| `0x142825C70` | `BSLightingShaderMaterial::Func3` | `BSLightingShaderMaterialBase::CopyMembers` |
| `0x14278F8F0` | `BSScript::UnlinkedTypes::CollectStringsFunctor::Func2` | `BSScript::UnlinkedTypes::_anon_70296790::CollectStringsFunctor::operator` |
| `0x14278F780` | `BSScript::UnlinkedTypes::UnpackInstructionsFunctor::Func2` | `BSScript::UnlinkedTypes::_anon_70296790::UnpackInstructionsFunctor::operator` |
| `0x14278F470` | `BSScript::UnlinkedTypes::WriteInstructionsFunctor::Func2` | `BSScript::UnlinkedTypes::_anon_70296790::WriteInstructionsFunctor::operator` |
| `0x14278F2D0` | `BSScript::UnlinkedTypes::CountInstructionInfo::Func2` | `BSScript::UnlinkedTypes::_anon_70296790::CountInstructionInfo::operator` |
| `0x1425D4630` | `BSPlatform::BSAsyncRequest_std::function_void::::Func1` | `BSPlatform::BSAsyncRequest<std::function<void __cdecl(BSPlatform::BNetResult,unsigned _...` |
| `0x14251A950` | `NavMeshSearchHide::Func2` | `BSNavmeshSearch::GetNodeConnections` |
| `0x1424ECA50` | `MovementControllerNPC::Func5` | `MovementControllerAI::UnregisterInterface` |
| `0x1424EC990` | `MovementControllerNPC::Func3` | `MovementControllerAI::GetInterface` |
| `0x1424EC8D0` | `MovementControllerNPC::Func4` | `MovementControllerAI::GetInterface` |
| `0x1424EC860` | `MovementControllerNPC::Func2` | `MovementControllerAI::RegisterInterface` |
| `0x1424EC720` | `MovementControllerNPC::Func9` | `MovementControllerAI::UpdateLargeDelta` |
| `0x1424EC5A0` | `MovementControllerNPC::Func8` | `MovementControllerAI::UpdateSmallDelta` |
| `0x1424EC560` | `MovementControllerNPC::Func6` | `MovementControllerAI::QCurrentState` |
| `0x1424C22B0` | `QuestPathingRequest::Func3` | `BSPathingRequest::CopyTo` |
| `0x1424C2230` | `CombatPathingRequest::Func2` | `BSPathingRequest::GetType` |
| `0x14233DB30` | `SFRenderBufferManager::Func6` | `Scaleform::Render::RBGenericImpl::RenderBufferManager::Reset` |
| `0x14233D800` | `SFRenderBufferManager::Func2` | `Scaleform::Render::RBGenericImpl::RenderBufferManager::Initialize` |
| `0x14233D680` | `SFRenderBufferManager::Func3` | `Scaleform::Render::RBGenericImpl::RenderBufferManager::Destroy` |
| `0x14233D140` | `SFRenderBufferManager::Func4` | `Scaleform::Render::RBGenericImpl::RenderBufferManager::BeginFrame` |
| `0x1422FD090` | `BSScaleformImageLoader::Func3` | `Scaleform::GFx::ImageCreator::LoadImageFile` |
| `0x1422FCF20` | `BSScaleformImageLoader::Func4` | `Scaleform::GFx::ImageCreator::LoadExportedImage` |
| `0x141D03C10` | `BSSphereConstructor::Func3` | `BSShapeConstructor::CreateWireData` |
| `0x141CA8970` | `BSQueuedResourceCollection_BSResource::EntryDB_BSModelDB:...` | `BSQueuedResourceCollectionBase::GetDescription` |
| `0x141CA4C50` | `AttachDistant3DTask::Func2` | `IOTask::GetDescription` |
| `0x141C98170` | `BSTextureDB::QueuedHandlesWithCallback::Func2` | `BSTextureDB::QueuedHandles::GetDescription` |
| `0x141BB7590` | `BSSmallBlockAllocatorUtil::TLockingUserPool_24::_BSPointe...` | `BSSmallBlockAllocatorUtil::TLockingUserPool<24,_anon_D3F40217::BSPointerAllocatorPageAl...` |
| `0x141B818C0` | `BSResource::GlobalLocations::Func4` | `BSResource::_anon_91044A15::GlobalLocations::DoCreateStream` |
| `0x141AF4320` | `BSCXAPOWrapper::Func1` | `CXAPOBase::QueryInterface` |
| `0x141455950` | `BSScript::NativeLatentFunction3_BSScript::StaticFunctionT...` | `BSScript::NativeLatentFunction3<BSScript::StaticFunctionTag,BSScript::Variable,BSFixedS...` |
| `0x1414557D0` | `BSScript::NativeFunction3_BSScript::StaticFunctionTag_boo...` | `BSScript::NativeFunction3<BSScript::StaticFunctionTag,bool,BSFixedString const & __ptr6...` |
| `0x141455770` | `BSScript::NativeFunction3_BSScript::StaticFunctionTag_voi...` | `BSScript::NativeFunction3<BSScript::StaticFunctionTag,void,BSFixedString const & __ptr6...` |
| `0x14143AEF0` | `GameScript::ConcreteDelayFunctorFactory_GameScript::WaitF...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::WaitForResourceReca...` |
| `0x14143AE50` | `GameScript::ConcreteDelayFunctorFactory_GameScript::WaitF...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::WaitFor3DFunctor,24...` |
| `0x14143ADB0` | `GameScript::ConcreteDelayFunctorFactory_GameScript::SetSc...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::SetScaleFunctor,11>...` |
| `0x14143AD00` | `GameScript::ConcreteDelayFunctorFactory_GameScript::SetPo...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::SetPositionFunctor,...` |
| `0x14143AC50` | `GameScript::ConcreteDelayFunctorFactory_GameScript::SetMo...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::SetMotionTypeFuncto...` |
| `0x14143ABA0` | `GameScript::ConcreteDelayFunctorFactory_GameScript::SetAn...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::SetAngleFunctor,7>:...` |
| `0x14143AB00` | `GameScript::ConcreteDelayFunctorFactory_GameScript::Reset...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::ResetFunctor,17>::C...` |
| `0x14143AA60` | `GameScript::ConcreteDelayFunctorFactory_GameScript::Remov...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::RemoveItemFunctor,2...` |
| `0x14143A9C0` | `GameScript::ConcreteDelayFunctorFactory_GameScript::Remov...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::RemoveComponentFunc...` |
| `0x14143A920` | `GameScript::ConcreteDelayFunctorFactory_GameScript::NonLa...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::NonLatentDeleteFunc...` |
| `0x14143A880` | `GameScript::ConcreteDelayFunctorFactory_GameScript::MoveT...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::MoveToOwnEditorLocF...` |
| `0x14143A7E0` | `GameScript::ConcreteDelayFunctorFactory_GameScript::MoveT...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::MoveToNearestNavmes...` |
| `0x14143A720` | `GameScript::ConcreteDelayFunctorFactory_GameScript::MoveT...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::MoveToFunctor,0>::C...` |
| `0x14143A680` | `GameScript::ConcreteDelayFunctorFactory_GameScript::Force...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::ForceAddRemoveRagdo...` |
| `0x14143A5D0` | `GameScript::ConcreteDelayFunctorFactory_GameScript::Enabl...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::EnableFunctor,3>::C...` |
| `0x14143A530` | `GameScript::ConcreteDelayFunctorFactory_GameScript::DropR...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::DropRefFunctor,30>:...` |
| `0x14143A490` | `GameScript::ConcreteDelayFunctorFactory_GameScript::DropO...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::DropObjectFunctor,1...` |
| `0x14143A3F0` | `GameScript::ConcreteDelayFunctorFactory_GameScript::Disab...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::DisableFunctor,4>::...` |
| `0x14143A350` | `GameScript::ConcreteDelayFunctorFactory_GameScript::Delet...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::DeleteFunctor,5>::C...` |
| `0x14143A2B0` | `GameScript::ConcreteDelayFunctorFactory_GameScript::Damag...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::DamageObjectFunctor...` |
| `0x14143A210` | `GameScript::ConcreteDelayFunctorFactory_GameScript::Clear...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::ClearDestructionFun...` |
| `0x14143A170` | `GameScript::ConcreteDelayFunctorFactory_GameScript::Apply...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::ApplyHavokImpulseFu...` |
| `0x14143A0C0` | `GameScript::ConcreteDelayFunctorFactory_GameScript::AddIt...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::AddItemFunctor,19>:...` |
| `0x1414301A0` | `BSScript::NativeFunction2_TESObjectREFR_bool_TESForm_BGSM...` | `BSScript::NativeFunction2<TESObjectREFR,bool,TESForm * __ptr64,BGSMod::Attachment::Mod ...` |
| `0x14142FEA0` | `BSScript::NativeFunction2_TESObjectREFR_void_TESForm_BGSM...` | `BSScript::NativeFunction2<TESObjectREFR,void,TESForm * __ptr64,BGSMod::Attachment::Mod ...` |
| `0x14142F720` | `BSScript::NativeFunction2_GameScript::RefrOrInventoryObj_...` | `BSScript::NativeFunction2<GameScript::RefrOrInventoryObj,bool,BGSMod::Attachment::Mod *...` |
| `0x14142EA00` | `BSScript::NativeFunction1_GameScript::RefrOrInventoryObj_...` | `BSScript::NativeFunction1<GameScript::RefrOrInventoryObj,void,BGSMod::Attachment::Mod *...` |
| `0x14142DFB0` | `GameScript::ConcreteDelayFunctorFactory_GameScript::WaitF...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::WaitForResourceReca...` |
| `0x14142DF40` | `GameScript::ConcreteDelayFunctorFactory_GameScript::WaitF...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::WaitFor3DFunctor,24...` |
| `0x14142DED0` | `GameScript::ConcreteDelayFunctorFactory_GameScript::SetSc...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::SetScaleFunctor,11>...` |
| `0x14142DE60` | `GameScript::ConcreteDelayFunctorFactory_GameScript::SetPo...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::SetPositionFunctor,...` |
| `0x14142DDF0` | `GameScript::ConcreteDelayFunctorFactory_GameScript::SetMo...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::SetMotionTypeFuncto...` |
| `0x14142DD80` | `GameScript::ConcreteDelayFunctorFactory_GameScript::SetAn...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::SetAngleFunctor,7>:...` |
| `0x14142DD10` | `GameScript::ConcreteDelayFunctorFactory_GameScript::Reset...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::ResetFunctor,17>::`...` |
| `0x14142DCA0` | `GameScript::ConcreteDelayFunctorFactory_GameScript::Remov...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::RemoveItemFunctor,2...` |
| `0x14142DC30` | `GameScript::ConcreteDelayFunctorFactory_GameScript::Remov...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::RemoveComponentFunc...` |
| `0x14142DBC0` | `GameScript::ConcreteDelayFunctorFactory_GameScript::NonLa...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::NonLatentDeleteFunc...` |
| `0x14142DB50` | `GameScript::ConcreteDelayFunctorFactory_GameScript::MoveT...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::MoveToOwnEditorLocF...` |
| `0x14142DAE0` | `GameScript::ConcreteDelayFunctorFactory_GameScript::MoveT...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::MoveToNearestNavmes...` |
| `0x14142DA70` | `GameScript::ConcreteDelayFunctorFactory_GameScript::MoveT...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::MoveToFunctor,0>::`...` |
| `0x14142DA00` | `GameScript::ConcreteDelayFunctorFactory_GameScript::Force...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::ForceAddRemoveRagdo...` |
| `0x14142D990` | `GameScript::ConcreteDelayFunctorFactory_GameScript::Enabl...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::EnableFunctor,3>::`...` |
| `0x14142D920` | `GameScript::ConcreteDelayFunctorFactory_GameScript::DropR...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::DropRefFunctor,30>:...` |
| `0x14142D8B0` | `GameScript::ConcreteDelayFunctorFactory_GameScript::DropO...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::DropObjectFunctor,1...` |
| `0x14142D840` | `GameScript::ConcreteDelayFunctorFactory_GameScript::Disab...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::DisableFunctor,4>::...` |
| `0x14142D7D0` | `GameScript::ConcreteDelayFunctorFactory_GameScript::Delet...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::DeleteFunctor,5>::`...` |
| `0x14142D760` | `GameScript::ConcreteDelayFunctorFactory_GameScript::Damag...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::DamageObjectFunctor...` |
| `0x14142D6F0` | `GameScript::ConcreteDelayFunctorFactory_GameScript::Clear...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::ClearDestructionFun...` |
| `0x14142D680` | `GameScript::ConcreteDelayFunctorFactory_GameScript::Apply...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::ApplyHavokImpulseFu...` |
| `0x14142D610` | `GameScript::ConcreteDelayFunctorFactory_GameScript::AddIt...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_4E78AFC4::AddItemFunctor,19>:...` |
| `0x14142D3F0` | `GameScript::SetScaleFunctor::Func2` | `GameScript::_anon_4E78AFC4::SetScaleFunctor::operator` |
| `0x14142D160` | `GameScript::SetMotionTypeFunctor::Func2` | `GameScript::_anon_4E78AFC4::SetMotionTypeFunctor::operator` |
| `0x141402770` | `GameScript::ClearDestructionFunctor::Func2` | `GameScript::_anon_4E78AFC4::ClearDestructionFunctor::operator` |
| `0x141402530` | `GameScript::WaitForResourceRecalcFunctor::Func2` | `GameScript::_anon_4E78AFC4::WaitForResourceRecalcFunctor::operator` |
| `0x1414022E0` | `GameScript::WaitFor3DFunctor::Func2` | `GameScript::_anon_4E78AFC4::WaitFor3DFunctor::operator` |
| `0x141401E70` | `GameScript::ResetFunctor::Func2` | `GameScript::_anon_4E78AFC4::ResetFunctor::operator` |
| `0x141401790` | `GameScript::ApplyHavokImpulseFunctor::Func2` | `GameScript::_anon_4E78AFC4::ApplyHavokImpulseFunctor::operator` |
| `0x141401420` | `GameScript::ForceAddRemoveRagdollFunctor::Func2` | `GameScript::_anon_4E78AFC4::ForceAddRemoveRagdollFunctor::operator` |
| `0x141400FE0` | `GameScript::DropObjectFunctor::Func2` | `GameScript::_anon_4E78AFC4::DropObjectFunctor::operator` |
| `0x141400710` | `GameScript::DeleteFunctor::Func2` | `GameScript::_anon_4E78AFC4::DeleteFunctor::operator` |
| `0x1414003F0` | `GameScript::DisableFunctor::Func2` | `GameScript::_anon_4E78AFC4::DisableFunctor::operator` |
| `0x141400070` | `GameScript::EnableFunctor::Func2` | `GameScript::_anon_4E78AFC4::EnableFunctor::operator` |
| `0x1413FFA90` | `GameScript::DamageObjectFunctor::Func2` | `GameScript::_anon_4E78AFC4::DamageObjectFunctor::operator` |
| `0x1413FEC20` | `GameScript::MoveToBaseFunctor::Func2` | `GameScript::_anon_4E78AFC4::MoveToBaseFunctor::operator` |
| `0x1413FE430` | `GameScript::DropRefFunctor::Func2` | `GameScript::_anon_4E78AFC4::DropRefFunctor::operator` |
| `0x1413FD9F0` | `GameScript::RemoveComponentFunctor::Func2` | `GameScript::_anon_4E78AFC4::RemoveComponentFunctor::operator` |
| `0x1413FD460` | `GameScript::RemoveItemFunctor::Func2` | `GameScript::_anon_4E78AFC4::RemoveItemFunctor::operator` |
| `0x1413FC330` | `GameScript::AddItemFunctor::Func2` | `GameScript::_anon_4E78AFC4::AddItemFunctor::operator` |
| `0x1413ED320` | `GameScript::ConcreteDelayFunctorFactory_GameScript::SendP...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_06587059::SendPlayerToJailFun...` |
| `0x1413EC400` | `GameScript::ConcreteDelayFunctorFactory_GameScript::SendP...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_06587059::SendPlayerToJailFun...` |
| `0x1413E9EF0` | `GameScript::SendPlayerToJailFunctor::Func2` | `GameScript::_anon_06587059::SendPlayerToJailFunctor::operator` |
| `0x1413E8BC0` | `GameScript::ConcreteDelayFunctorFactory_GameScript::CastF...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_82CC1904::CastFunctor,21>::Cr...` |
| `0x1413E8670` | `GameScript::ConcreteDelayFunctorFactory_GameScript::CastF...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_82CC1904::CastFunctor,21>::`v...` |
| `0x1413E73D0` | `GameScript::CastFunctor::Func2` | `GameScript::_anon_82CC1904::CastFunctor::operator` |
| `0x1413E68C0` | `GameScript::ConcreteDelayFunctorFactory_GameScript::Scrol...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_55CEF62F::ScrollCastFunctor,2...` |
| `0x1413E6630` | `GameScript::ConcreteDelayFunctorFactory_GameScript::Scrol...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_55CEF62F::ScrollCastFunctor,2...` |
| `0x1413E5950` | `GameScript::ScrollCastFunctor::Func2` | `GameScript::_anon_55CEF62F::ScrollCastFunctor::operator` |
| `0x1413E1710` | `BSScript::NativeLatentFunction2_BSScript::Object_void_BSF...` | `BSScript::NativeLatentFunction2<BSScript::Object,void,BSFixedString const & __ptr64,BSS...` |
| `0x1413E16B0` | `BSScript::NativeLatentFunction2_BSScript::Object_BSScript...` | `BSScript::NativeLatentFunction2<BSScript::Object,BSScript::Variable,BSFixedString const...` |
| `0x1413E1650` | `BSScript::NativeLatentFunction1_BSScript::Object_BSScript...` | `BSScript::NativeLatentFunction1<BSScript::Object,BSScript::Variable,BSFixedString const...` |
| `0x1413E15F0` | `BSScript::NativeFunction9_BSScript::Object_void_BSScript:...` | `BSScript::NativeFunction9<BSScript::Object,void,BSScript::Object * __ptr64,BSScript::Ob...` |
| `0x1413E1590` | `BSScript::NativeFunction4_BSScript::Object_void_TESObject...` | `BSScript::NativeFunction4<BSScript::Object,void,TESObjectREFR * __ptr64,TESObjectREFR *...` |
| `0x1413E1530` | `BSScript::NativeFunction4_BSScript::Object_void_BSScript:...` | `BSScript::NativeFunction4<BSScript::Object,void,BSScript::Object * __ptr64,BSScript::Ob...` |
| `0x1413E14D0` | `BSScript::NativeFunction3_BSScript::Object_void_BSScript:...` | `BSScript::NativeFunction3<BSScript::Object,void,BSScript::Object * __ptr64,BSScript::Ob...` |
| `0x1413E1470` | `BSScript::NativeFunction2_BSScript::Object_bool_TESObject...` | `BSScript::NativeFunction2<BSScript::Object,bool,TESObjectREFR * __ptr64,BSFixedString c...` |
| `0x1413E1410` | `BSScript::NativeFunction2_BSScript::Object_bool_BSScript:...` | `BSScript::NativeFunction2<BSScript::Object,bool,BSScript::Object * __ptr64,BSFixedStrin...` |
| `0x1413E13B0` | `BSScript::NativeFunction2_BSScript::Object_bool_BSFixedSt...` | `BSScript::NativeFunction2<BSScript::Object,bool,BSFixedString const & __ptr64,BSScript:...` |
| `0x1413E1350` | `BSScript::NativeFunction2_BSScript::Object_bool_BSFixedSt...` | `BSScript::NativeFunction2<BSScript::Object,bool,BSFixedString const & __ptr64,BSScript:...` |
| `0x1413E12F0` | `BSScript::NativeFunction2_BSScript::Object_void_TESObject...` | `BSScript::NativeFunction2<BSScript::Object,void,TESObjectREFR * __ptr64,TESObjectREFR *...` |
| `0x1413E1290` | `BSScript::NativeFunction2_BSScript::Object_void_TESObject...` | `BSScript::NativeFunction2<BSScript::Object,void,TESObjectREFR * __ptr64,BSFixedString c...` |
| `0x1413E1230` | `BSScript::NativeFunction2_BSScript::Object_void_Actor_TES...` | `BSScript::NativeFunction2<BSScript::Object,void,Actor * __ptr64,TESObjectREFR * __ptr64...` |
| `0x1413E11D0` | `BSScript::NativeFunction2_BSScript::Object_void_BSScript:...` | `BSScript::NativeFunction2<BSScript::Object,void,BSScript::Object * __ptr64,BSScript::Ob...` |
| `0x1413E1170` | `BSScript::NativeFunction2_BSScript::Object_void_BSScript:...` | `BSScript::NativeFunction2<BSScript::Object,void,BSScript::Object * __ptr64,BSFixedStrin...` |
| `0x1413E10B0` | `BSScript::NativeFunction2_BSScript::Object_void_BSFixedSt...` | `BSScript::NativeFunction2<BSScript::Object,void,BSFixedString const & __ptr64,BSScript:...` |
| `0x1413E1050` | `BSScript::NativeFunction2_BSScript::Object_void_BSFixedSt...` | `BSScript::NativeFunction2<BSScript::Object,void,BSFixedString const & __ptr64,BSScript:...` |
| `0x1413E0FF0` | `BSScript::NativeFunction2_BSScript::Object_void_BSFixedSt...` | `BSScript::NativeFunction2<BSScript::Object,void,BSFixedString const & __ptr64,int>::`ve...` |
| `0x1413E0F90` | `BSScript::NativeFunction1_BSScript::Object_bool_BSFixedSt...` | `BSScript::NativeFunction1<BSScript::Object,bool,BSFixedString const & __ptr64>::`vector...` |
| `0x1413E0F30` | `BSScript::NativeFunction1_BSScript::Object_void_TESForm_:...` | `BSScript::NativeFunction1<BSScript::Object,void,TESForm * __ptr64>::`scalar deleting de...` |
| `0x1413E0ED0` | `BSScript::NativeFunction1_BSScript::Object_void_BSScript:...` | `BSScript::NativeFunction1<BSScript::Object,void,BSScript::Object * __ptr64>::`vector de...` |
| `0x1413E0E10` | `BSScript::NativeFunction1_BSScript::Object_void_BSFixedSt...` | `BSScript::NativeFunction1<BSScript::Object,void,BSFixedString const & __ptr64>::`scalar...` |
| `0x1413E0DB0` | `BSScript::NativeFunction1_BSScript::Object_BSTSmartPointe...` | `BSScript::NativeFunction1<BSScript::Object,BSTSmartPointer<BSScript::Object,BSTSmartPoi...` |
| `0x1413CBF50` | `GameScript::ConcreteDelayFunctorFactory_GameScript::PlayB...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_5EE126CB::PlayBinkFunctor,25>...` |
| `0x1413C9B20` | `GameScript::ConcreteDelayFunctorFactory_GameScript::PlayB...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_5EE126CB::PlayBinkFunctor,25>...` |
| `0x1413BFD40` | `GameScript::PlayBinkFunctor::Func2` | `GameScript::_anon_5EE126CB::PlayBinkFunctor::operator` |
| `0x1413BD350` | `BSScript::NativeFunction2_BSScript::StaticFunctionTag_voi...` | `BSScript::NativeFunction2<BSScript::StaticFunctionTag,void,BSFixedString const & __ptr6...` |
| `0x1413BD110` | `BSScript::NativeFunction1_BSScript::StaticFunctionTag_voi...` | `BSScript::NativeFunction1<BSScript::StaticFunctionTag,void,BSScript::Object * __ptr64>:...` |
| `0x14139EF70` | `GameScript::ConcreteDelayFunctorFactory_GameScript::Resur...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_930AEBC0::ResurrectFunctor,20...` |
| `0x14139EED0` | `GameScript::ConcreteDelayFunctorFactory_GameScript::MoveT...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_930AEBC0::MoveToPackLocFuncto...` |
| `0x14139EE30` | `GameScript::ConcreteDelayFunctorFactory_GameScript::Attac...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_930AEBC0::AttachAshPileFuncto...` |
| `0x141399A70` | `GameScript::ConcreteDelayFunctorFactory_GameScript::Resur...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_930AEBC0::ResurrectFunctor,20...` |
| `0x141399A00` | `GameScript::ConcreteDelayFunctorFactory_GameScript::MoveT...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_930AEBC0::MoveToPackLocFuncto...` |
| `0x141399990` | `GameScript::ConcreteDelayFunctorFactory_GameScript::Attac...` | `GameScript::ConcreteDelayFunctorFactory<GameScript::_anon_930AEBC0::AttachAshPileFuncto...` |
| `0x141389C60` | `GameScript::MoveToPackLocFunctor::Func2` | `GameScript::_anon_930AEBC0::MoveToPackLocFunctor::operator` |
| `0x141386FA0` | `GameScript::ResurrectFunctor::Func2` | `GameScript::_anon_930AEBC0::ResurrectFunctor::operator` |
| `0x141386760` | `GameScript::AttachAshPileFunctor::Func2` | `GameScript::_anon_930AEBC0::AttachAshPileFunctor::operator` |
| `0x141347EE0` | `GameScript::ProfileCallQueryTranslator::Func1` | `GameScript::_anon_95C9DED5::ProfileCallQueryTranslator::GetSelfVariable` |
| `0x1411EE2F0` | `CombatPathingRequestCovered::Func5` | `CombatPathingRequest::Read` |
| `0x1411EE280` | `CombatPathingRequestCovered::Func4` | `CombatPathingRequest::Write` |
| `0x1411E37E0` | `CombatPathBuilderT_CombatPathBuilderStandard_CombatPathin...` | `CombatPathBuilder::BuildPath` |
| `0x1411A6F90` | `CombatBehaviorTreeNodeObject1_CombatBehaviorIdle_CombatBe...` | `CombatBehaviorTreeNodeObject1<CombatBehaviorIdle,CombatBehaviorExpression<CombatBehavio...` |
| `0x1411A6E70` | `CombatBehaviorTreeConditionalNode_CombatBehaviorExpressio...` | `CombatBehaviorTreeConditionalNode<CombatBehaviorExpression<CombatBehaviorUnaryExpressio...` |
| `0x1411937F0` | `CombatBehaviorTreeValueNodeT_float_CombatBehaviorExpressi...` | `CombatBehaviorTreeValueNodeT<float,CombatBehaviorExpression<CombatBehaviorFunc2<float (...` |
| `0x141193240` | `CombatBehaviorTreeNodeObject1_CombatBehaviorComposite_Com...` | `CombatBehaviorTreeNodeObject1<CombatBehaviorComposite<CombatBehaviorMovementIdle,Combat...` |
| `0x141187B40` | `CombatBehaviorTreeValueNodeT_unsignedint_CombatBehaviorEx...` | `CombatBehaviorTreeValueNodeT<unsigned int,CombatBehaviorExpression<CombatBehaviorUnaryE...` |
| `0x141187AE0` | `CombatBehaviorTreeValueNodeT_unsignedint_CombatBehaviorEx...` | `CombatBehaviorTreeValueNodeT<unsigned int,CombatBehaviorExpression<CombatBehaviorFunc1<...` |
| `0x14115E060` | `CombatBehaviorTreeValueNodeT_unsignedint_CombatBehaviorEx...` | `CombatBehaviorTreeValueNodeT<unsigned int,CombatBehaviorExpression<CombatBehaviorUnaryE...` |
| `0x14115DF40` | `CombatBehaviorTreeValueNodeT_unsignedint_CombatBehaviorEx...` | `CombatBehaviorTreeValueNodeT<unsigned int,CombatBehaviorExpression<CombatBehaviorBinary...` |
| `0x141158360` | `CombatBehaviorTreeConditionalNode_CombatBehaviorExpressio...` | `CombatBehaviorTreeConditionalNode<CombatBehaviorExpression<CombatBehaviorUnaryExpressio...` |
| `0x1410F5820` | `CombatBehaviorTreeConditionalNode_CombatBehaviorExpressio...` | `CombatBehaviorTreeConditionalNode<CombatBehaviorExpression<CombatBehaviorFunc1<bool (__...` |
| `0x140DE5F50` | `TESObjectREFR::HandleOutfitItemsFunctor::Func2` | `EquipActorOutfitItemsFunctor::operator` |
| `0x140C8AB70` | `AbsorbEffect::Func3` | `ValueModifierEffect::OnAdd` |
| `0x140C634E0` | `ActorMagicCaster::Func4` | `MagicCaster::FindTouchTarget` |
| `0x140C63390` | `ActorMagicCaster::Func3` | `MagicCaster::CastSpellImmediate` |
| `0x140C4EA50` | `CloakEffect::Func3` | `ActiveEffect::OnAdd` |
| `0x140C4E860` | `AbsorbEffect::Func2` | `ActiveEffect::AdjustForPerks` |
| `0x140A4E730` | `HUDCriticalMeter::Func3` | `HUDMeter::ProcessHUDMessage` |
| `0x1409F9650` | `CheckHardcoreFastTravelCommand::Func3` | `PipboyCommand::DoValidate` |
| `0x140896140` | `CombatBehaviorTreeConditionalNode_CombatBehaviorExpressio...` | `CombatBehaviorTreeConditionalNode<CombatBehaviorExpression<CombatBehaviorFunc1<bool (__...` |
| `0x14088BD60` | `CombatBehaviorTreeConditionalNode_CombatBehaviorExpressio...` | `CombatBehaviorTreeConditionalNode<CombatBehaviorExpression<CombatBehaviorFunc1<bool (__...` |
| `0x140867380` | `CombatBehaviorTreeValueNodeT_float_CombatBehaviorExpressi...` | `CombatBehaviorTreeValueNodeT<float,CombatBehaviorExpression<CombatBehaviorFunc1<float (...` |
| `0x1407DA4E0` | `hkSignal2_hknpEventHandlerInputconst_hknpEventconst::_Mem...` | `hkSignal2<hknpEventHandlerInput const & __ptr64,hknpEvent const & __ptr64>::MemberSlot<...` |
| `0x1407DA4A0` | `hkSignal2_hknpEventHandlerInputconst_hknpEventconst::_Mem...` | `hkSignal2<hknpEventHandlerInput const & __ptr64,hknpEvent const & __ptr64>::MemberSlot<...` |
| `0x1407DA460` | `hkSignal2_hknpEventHandlerInputconst_hknpEventconst::_Mem...` | `hkSignal2<hknpEventHandlerInput const & __ptr64,hknpEvent const & __ptr64>::MemberSlot<...` |
| `0x1407986F0` | `BSPathfindingTemporary_PathingRequestSafeStraightLine_::F...` | `PathingRequestSafeStraightLine::GetType` |
| `0x1407986B0` | `BSPathfindingTemporary_PathingRequestSafeStraightLine_::F...` | `PathingRequestSafeStraightLine::CopyTo` |
| `0x14078E920` | `BSPathfindingTemporary_PathingRequestClosePoint_::Func5` | `PathingRequestClosePoint::Read` |
| `0x14078E880` | `BSPathfindingTemporary_PathingRequestClosePoint_::Func4` | `PathingRequestClosePoint::Write` |
| `0x14078E800` | `BSPathfindingTemporary_PathingRequestClosePoint_::Func2` | `PathingRequestClosePoint::GetType` |
| `0x14078E700` | `BSPathfindingTemporary_PathingRequestClosePoint_::Func3` | `PathingRequestClosePoint::CopyTo` |
| `0x14077B690` | `BSPathingRequest::Func5` | `PathingRequest::Read` |
| `0x14077B680` | `BSPathingRequest::Func4` | `PathingRequest::Write` |
| `0x14077B600` | `BSPathfindingTemporary_PathingRequest_::Func2` | `PathingRequest::GetType` |
| `0x140705990` | `BGSProcedureTreeBranch::Func8` | `BGSProcedureTreeConditionalItem::CreateExecState` |
| `0x14067ACA0` | `QueuedHead::Func2` | `BSFaceGenPendingHeadData::GetDescription` |
| `0x14062A370` | `REFREventCallbacks::ConcreteEventCallbackFactory::_TopicR...` | `REFREventCallbacks::ConcreteEventCallbackFactory<_anon_F1F510FF::TopicResultCallBack>::...` |
| `0x140629A80` | `REFREventCallbacks::ConcreteEventCallbackFactory::_TopicR...` | `REFREventCallbacks::ConcreteEventCallbackFactory<_anon_F1F510FF::TopicResultCallBack>::...` |
| `0x1405FA750` | `REFREventCallbacks::ConcreteEventCallbackFactory::_StageI...` | `REFREventCallbacks::ConcreteEventCallbackFactory<_anon_9400FB4A::StageItemFinishedCallb...` |
| `0x1405FA540` | `REFREventCallbacks::ConcreteEventCallbackFactory::_StageI...` | `REFREventCallbacks::ConcreteEventCallbackFactory<_anon_9400FB4A::StageItemFinishedCallb...` |
| `0x1405A04C0` | `REFREventCallbacks::ConcreteEventCallbackFactory::_SceneP...` | `REFREventCallbacks::ConcreteEventCallbackFactory<_anon_998808F9::ScenePhaseResultsCallB...` |
| `0x1405A0370` | `REFREventCallbacks::ConcreteEventCallbackFactory::_SceneP...` | `REFREventCallbacks::ConcreteEventCallbackFactory<_anon_998808F9::ScenePhaseResultsCallB...` |
| `0x14059E2D0` | `REFREventCallbacks::ConcreteEventCallbackFactory::_SceneA...` | `REFREventCallbacks::ConcreteEventCallbackFactory<_anon_5E5812FC::SceneActionResultsCall...` |
| `0x14059E130` | `REFREventCallbacks::ConcreteEventCallbackFactory::_SceneA...` | `REFREventCallbacks::ConcreteEventCallbackFactory<_anon_5E5812FC::SceneActionResultsCall...` |
| `0x140597750` | `BGSSceneActionConversationBase::Func5` | `BGSSceneAction::ClearData` |
| `0x1405950D0` | `REFREventCallbacks::ConcreteEventCallbackFactory::_SceneR...` | `REFREventCallbacks::ConcreteEventCallbackFactory<_anon_09B531BE::SceneResultsCallBack>:...` |
| `0x140594B70` | `REFREventCallbacks::ConcreteEventCallbackFactory::_SceneR...` | `REFREventCallbacks::ConcreteEventCallbackFactory<_anon_09B531BE::SceneResultsCallBack>:...` |
| `0x14057D2F0` | `BGSRefCollectionAlias::Func3` | `BGSRefAlias::InitItem` |
| `0x14054BBD0` | `FormSortFunc::` | `TESForm::FormSortFunc::operator` |
| `0x140137E50` | `TESModelDB::TESProcessor::Func2` | `TESModelDB::_anon_933957B6::TESProcessor::PostCreate` |
---

## Update 2026-07-10: the 202 engine reassignments were decompile-reviewed

All 202 were batch-decompiled and reviewed by 4 parallel agents against the actual C body.
Result: **199 applied** (`reassign_apply.csv` → `applied_reassignments.csv`), split ~161
SAME_CLASS_FORMATTING (placeholder and decoded name are the same class, decoded just adds the full
template/anon-namespace spelling — many of the giant-template ones are un-nameable in Ghidra and
skip cleanly) + ~38 APPLY_REASSIGNMENT (genuine inherited base-class methods the derived class's
vtable slot points at, e.g. `MovementControllerNPC::FuncN` → `MovementControllerAI::UpdateSmallDelta`,
`PathingRequestClosePoint::FuncN` → base `BSPathingRequest::Read`, confirmed by the body calling
base-class helpers or CRC'ing the literal base-class type-name string).

**3 held (do NOT apply without new evidence):**

| Address | Placeholder | Proposed decoded | Why held |
|---|---|---|---|
| `0x14067ACA0` | `BSFaceGenPendingHeadData`-adjacent QueuedHead | `BSFaceGenPendingHeadData::GetDescription` | KEEP_PLACEHOLDER — body formats "Invalid QueuedHead"/"Queued head for NPC %s", strings specific to the DERIVED `QueuedHead` class, so the base-class name would be a misname. |
| `0x140DE5F50` | outfit-items functor | `HandleOutfitItemsFunctor` vs `EquipActorOutfitItemsFunctor` | INCONCLUSIVE — trivial thunk (operator() recursively calling operator()), no evidence either way. |
| `0x140705990` | proc-tree branch/conditional | `BGSProcedureTreeBranch` vs `BGSProcedureTreeConditionalItem` | INCONCLUSIVE — generic `BSTFactoryManager` exec-state dispatch with nothing specific to either class. |
