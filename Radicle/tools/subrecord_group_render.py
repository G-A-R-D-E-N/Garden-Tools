import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from field_address_codegen import cpp_signature, cpp_string  # noqa: E402


def render_group_members(members, lines, defaults):
    specs = []
    for member in members:
        spec = (f"{cpp_signature(member['subrecord'])}, "
                f"{'true' if member['required'] else 'false'}, "
                f"{'true' if member['repeatable'] else 'false'}, {member['payload_size']}, ")
        if member["default"] is not None:
            name = f"k_subrecord_group_default_{defaults[0]}"
            defaults[0] += 1
            body = ", ".join(f"0x{byte:02X}" for byte in member["default"])
            lines.append(f"constexpr std::uint8_t {name}[] = {{{body}}};")
            spec += f"std::span<const std::uint8_t>({name})"
        else:
            spec += "std::span<const std::uint8_t>{}"
        spec += f", {'true' if member['localized'] else 'false'}"
        spec += f", {'true' if member['skippable'] else 'false'}"
        spec += f", {'true' if member['leading'] else 'false'}"
        specs.append("{" + spec + "},")
    return specs


def render_subrecord_groups(groups):
    lines = []
    entries = []
    defaults = [0]
    for index, group in enumerate(groups):
        members = render_group_members(group["members"], lines, defaults)
        if group["successors"]:
            body = ", ".join(f"Signature::from_string({cpp_string(signature)})" for signature in group["successors"])
            lines.append(f"constexpr Signature k_subrecord_group_successors_{index}[] = {{{body}}};")
            successors = f"std::span<const Signature>(k_subrecord_group_successors_{index})"
        else:
            successors = "std::span<const Signature>{}"
        lines.append(f"constexpr SubrecordGroupMemberDescriptor k_subrecord_group_members_{index}[] = {{")
        lines.extend(members)
        lines.append("};")
        if group["run"] is None:
            run = "SubrecordGroupRunDescriptor{}"
        else:
            run_members = render_group_members(group["run"]["members"], lines, defaults)
            lines.append(f"constexpr SubrecordGroupMemberDescriptor k_subrecord_group_run_members_{index}[] = {{")
            lines.extend(run_members)
            lines.append("};")
            counter = (f"Signature::from_string({cpp_string(group['run']['count_member'])})"
                       if group["run"]["count_member"] else "Signature{}")
            run = ("SubrecordGroupRunDescriptor{" + counter + ", " +
                   f"std::span<const SubrecordGroupMemberDescriptor>(k_subrecord_group_run_members_{index})" +
                   "}")
        entries.append(
            "{" +
            f"Signature::from_string({cpp_string(group['record'])}), " +
            f"{cpp_string(group['path'])}, " +
            f"{cpp_string(group['label'])}, " +
            (f"Signature::from_string({cpp_string(group['count_subrecord'])})"
             if group["count_subrecord"] else "Signature{}") + ", " +
            f"std::span<const SubrecordGroupMemberDescriptor>(k_subrecord_group_members_{index}), " +
            successors + ", " +
            run + ", " +
            ('true' if group["requires_existing_count"] else 'false') +
            "},")
    if groups:
        lines.append(f"constexpr std::array<SubrecordGroupDescriptor, {len(groups)}> k_subrecord_groups{{{{")
        lines.extend(entries)
        lines.append("}};")
    else:
        lines.append("constexpr std::array<SubrecordGroupDescriptor, 0> k_subrecord_groups{};")
    return lines
