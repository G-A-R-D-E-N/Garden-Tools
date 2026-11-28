#include <fo4/avm1/avm1.hpp>

#include <cstdint>

namespace fo4::avm1 {

namespace {

struct ActionName {
  std::uint8_t code;
  const char* name;
};

constexpr ActionName kNames[] = {
    {0x00, "End"},          {0x04, "NextFrame"},     {0x05, "PreviousFrame"},
    {0x06, "Play"},         {0x07, "Stop"},          {0x08, "ToggleQuality"},
    {0x09, "StopSounds"},   {0x0A, "Add"},           {0x0B, "Subtract"},
    {0x0C, "Multiply"},     {0x0D, "Divide"},        {0x0E, "Equals"},
    {0x0F, "Less"},         {0x10, "And"},           {0x11, "Or"},
    {0x12, "Not"},          {0x13, "StringEquals"},  {0x14, "StringLength"},
    {0x15, "StringExtract"}, {0x17, "Pop"},          {0x18, "ToInteger"},
    {0x1C, "GetVariable"},  {0x1D, "SetVariable"},   {0x20, "SetTarget2"},
    {0x21, "StringAdd"},    {0x22, "GetProperty"},   {0x23, "SetProperty"},
    {0x24, "CloneSprite"},  {0x25, "RemoveSprite"},  {0x26, "Trace"},
    {0x27, "StartDrag"},    {0x28, "EndDrag"},       {0x29, "StringLess"},
    {0x2A, "Throw"},        {0x2B, "CastOp"},        {0x2C, "ImplementsOp"},
    {0x30, "RandomNumber"}, {0x31, "MBStringLength"}, {0x32, "CharToAscii"},
    {0x33, "AsciiToChar"},  {0x34, "GetTime"},       {0x35, "MBStringExtract"},
    {0x36, "MBCharToAscii"}, {0x37, "MBAsciiToChar"}, {0x3A, "Delete"},
    {0x3B, "Delete2"},      {0x3C, "DefineLocal"},   {0x3D, "CallFunction"},
    {0x3E, "Return"},       {0x3F, "Modulo"},        {0x40, "NewObject"},
    {0x41, "DefineLocal2"}, {0x42, "InitArray"},     {0x43, "InitObject"},
    {0x44, "TypeOf"},       {0x45, "TargetPath"},    {0x46, "Enumerate"},
    {0x47, "Add2"},         {0x48, "Less2"},         {0x49, "Equals2"},
    {0x4A, "ToNumber"},     {0x4B, "ToString"},      {0x4C, "PushDuplicate"},
    {0x4D, "StackSwap"},    {0x4E, "GetMember"},     {0x4F, "SetMember"},
    {0x50, "Increment"},    {0x51, "Decrement"},     {0x52, "CallMethod"},
    {0x53, "NewMethod"},    {0x54, "InstanceOf"},    {0x55, "Enumerate2"},
    {0x60, "BitAnd"},       {0x61, "BitOr"},         {0x62, "BitXor"},
    {0x63, "ShiftLeft"},    {0x64, "ShiftRight"},    {0x65, "ShiftUnsigned"},
    {0x66, "StrictEquals"}, {0x67, "Greater"},       {0x68, "StringGreater"},
    {0x69, "Extends"},      {0x81, "GotoFrame"},     {0x83, "GetURL"},
    {0x87, "StoreRegister"}, {0x88, "ConstantPool"}, {0x89, "StrictMode"},
    {0x8A, "WaitForFrame"}, {0x8B, "SetTarget"},     {0x8C, "GoToLabel"},
    {0x8D, "WaitForFrame2"}, {0x8E, "DefineFunction2"}, {0x8F, "Try"},
    {0x94, "With"},         {0x96, "Push"},          {0x99, "Jump"},
    {0x9A, "GetURL2"},      {0x9B, "DefineFunction"}, {0x9D, "If"},
    {0x9E, "Call"},         {0x9F, "GotoFrame2"},
};

}

const char* action_mnemonic(std::uint8_t code) noexcept {
  for (const ActionName& entry : kNames) {
    if (entry.code == code) {
      return entry.name;
    }
  }
  return nullptr;
}

}  // namespace fo4::avm1
