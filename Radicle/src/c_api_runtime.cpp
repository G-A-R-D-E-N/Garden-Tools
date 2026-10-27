#include "fo4/c_api_runtime.h"

#include "c_api_env_internal.hpp"
#include "c_api_internal.hpp"
#include "fo4/abc/abc.hpp"
#include "fo4/avm2/host.hpp"
#include "fo4/avm2/interpreter.hpp"
#include "fo4/avm2/replay.hpp"
#include "fo4/avm2/runtime.hpp"
#include "fo4/avm2/snapshot.hpp"
#include "fo4/avm2/swf_runtime.hpp"
#include "fo4/swf/swf.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

using radicle_c_detail::clear_error;
using radicle_c_detail::fail;
using radicle_c_env_detail::guarded;

struct radicle_runtime {
  fo4::avm2::Runtime runtime;
  fo4::avm2::Interpreter interpreter{runtime};
  radicle_runtime_host host{};
  std::vector<std::shared_ptr<fo4::swf::SwfFile>> files;
  std::vector<std::shared_ptr<fo4::avm2::SwfRuntime>> movies;
};

struct radicle_movie {
  radicle_runtime* owner{nullptr};
  std::shared_ptr<fo4::avm2::SwfRuntime> movie;
};

struct radicle_runtime_snapshot {
  fo4::avm2::FrameSnapshot frame;
  std::vector<std::vector<radicle_runtime_run>> runs;
  std::vector<std::vector<std::vector<radicle_runtime_glyph>>> glyphs;
};

namespace {

std::optional<std::vector<std::uint8_t>> take_host_bytes(std::uint8_t* bytes, size_t size) {
  if (bytes == nullptr) {
    return std::nullopt;
  }
  std::vector<std::uint8_t> result(bytes, bytes + size);
  radicle_free_bytes(bytes);
  return result;
}

/* The host callbacks must be copied into the runtime once, so a caller may free its descriptor. */
void install_host(radicle_runtime& runtime, const radicle_runtime_host& host) {
  runtime.host = host;
  fo4::avm2::HostCallbacks callbacks;
  if (host.resolve_asset != nullptr) {
    callbacks.resolve_asset = [&runtime](std::string_view path)
        -> std::optional<std::vector<std::uint8_t>> {
      std::uint8_t* bytes = nullptr;
      size_t size = 0;
      const std::string text{path};
      const radicle_status status =
          runtime.host.resolve_asset(runtime.host.user, text.c_str(), &bytes, &size);
      if (status != RADICLE_STATUS_OK) {
        if (bytes != nullptr) radicle_free_bytes(bytes);
        return std::nullopt;
      }
      return take_host_bytes(bytes, size);
    };
  }
  if (host.read_storage != nullptr) {
    callbacks.read_storage = [&runtime](std::string_view name)
        -> std::optional<std::vector<std::uint8_t>> {
      std::uint8_t* bytes = nullptr;
      size_t size = 0;
      const std::string text{name};
      const radicle_status status =
          runtime.host.read_storage(runtime.host.user, text.c_str(), &bytes, &size);
      if (status != RADICLE_STATUS_OK) {
        if (bytes != nullptr) radicle_free_bytes(bytes);
        return std::nullopt;
      }
      return take_host_bytes(bytes, size);
    };
  }
  if (host.write_storage != nullptr) {
    callbacks.write_storage = [&runtime](std::string_view name, std::vector<std::uint8_t> bytes) {
      const std::string text{name};
      return runtime.host.write_storage(runtime.host.user, text.c_str(), bytes.data(),
                                        bytes.size()) == RADICLE_STATUS_OK;
    };
  }
  if (host.host_call != nullptr) {
    callbacks.host_call = [&runtime](std::string_view method,
                                     std::vector<std::string> arguments)
        -> std::optional<std::string> {
      std::vector<const char*> parts;
      parts.reserve(arguments.size());
      for (const std::string& argument : arguments) parts.push_back(argument.c_str());
      std::uint8_t* text = nullptr;
      size_t size = 0;
      const std::string method_text{method};
      const radicle_status status = runtime.host.host_call(
          runtime.host.user, method_text.c_str(), parts.data(), parts.size(), &text, &size);
      if (status != RADICLE_STATUS_OK) {
        if (text != nullptr) radicle_free_bytes(text);
        return std::nullopt;
      }
      if (text == nullptr) {
        radicle_free_bytes(text);
        return std::nullopt;
      }
      std::string answer(reinterpret_cast<const char*>(text), size);
      radicle_free_bytes(text);
      return answer;
    };
  }
  runtime.runtime.host().set_callbacks(std::move(callbacks));
}

bool read_movie_file(const std::uint8_t* bytes, size_t size,
                     std::shared_ptr<fo4::swf::SwfFile>& out_file) {
  if (bytes == nullptr || size == 0) return false;
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(bytes), size));
  if (!read.ok()) return false;
  out_file = std::make_shared<fo4::swf::SwfFile>(read.file);
  return true;
}

radicle_status open_movie_bytes(radicle_runtime& owner, const std::uint8_t* bytes, size_t size,
                                radicle_movie** out_movie) {
  std::shared_ptr<fo4::swf::SwfFile> file;
  if (!read_movie_file(bytes, size, file)) {
    return fail(RADICLE_STATUS_PARSE_ERROR, "the movie bytes do not parse");
  }
  std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(owner.runtime, owner.interpreter, *file);
  if (movie == nullptr) {
    return fail(RADICLE_STATUS_PARSE_ERROR,
                "the movie could not be created; a required class or script is missing");
  }
  owner.files.push_back(file);
  owner.movies.push_back(movie);
  auto handle = std::make_unique<radicle_movie>();
  handle->owner = &owner;
  handle->movie = std::move(movie);
  *out_movie = handle.release();
  return RADICLE_STATUS_OK;
}

fo4::avm2::ReplayInput to_replay(const radicle_runtime_input& input) {
  fo4::avm2::ReplayInput replay;
  replay.time_ms = input.time_ms;
  replay.kind = static_cast<fo4::avm2::ReplayInputKind>(input.kind);
  replay.x = input.x;
  replay.y = input.y;
  replay.button = input.button;
  replay.key_code = input.key_code;
  replay.delta = input.delta;
  return replay;
}

void fill_runs(const fo4::avm2::SnapshotEntry& source, radicle_runtime_snapshot& snapshot,
               std::size_t entry_index) {
  std::vector<radicle_runtime_run>& runs = snapshot.runs[entry_index];
  std::vector<std::vector<std::vector<radicle_runtime_glyph>>>& glyph_lists = snapshot.glyphs;
  runs.clear();
  runs.reserve(source.runs.size());
  for (const fo4::avm2::SnapshotTextRun& run : source.runs) {
    std::vector<radicle_runtime_glyph> glyphs;
    glyphs.reserve(run.glyphs.size());
    for (const fo4::avm2::SnapshotGlyph& glyph : run.glyphs) {
      glyphs.push_back(radicle_runtime_glyph{glyph.font_id, glyph.glyph_id, glyph.x, glyph.advance});
    }
    const std::size_t run_index = runs.size();
    runs.push_back(radicle_runtime_run{run.font_id, run.font_size, run.x, run.y, run.color, 0,
                                       nullptr});
    glyph_lists[entry_index].push_back(std::move(glyphs));
    if (!glyph_lists[entry_index].back().empty()) {
      runs[run_index].glyph_count = glyph_lists[entry_index].back().size();
      runs[run_index].glyphs = glyph_lists[entry_index].back().data();
    }
  }
}

void fill_entry(const fo4::avm2::SnapshotEntry& source, radicle_runtime_entry& out) {
  out = {};
  out.kind = static_cast<std::uint8_t>(source.kind);
  out.character_id = source.character_id;
  out.clip_depth = source.clip_depth;
  out.blend_mode = source.blend_mode;
  out.visible = source.visible ? 1 : 0;
  out.alpha = source.alpha;
  out.path = source.path.c_str();
  out.name = source.name.c_str();
  out.matrix = radicle_runtime_matrix{source.matrix.a, source.matrix.b, source.matrix.c,
                                      source.matrix.d, source.matrix.tx, source.matrix.ty};
  out.colour[0] = source.color.red_multiplier;
  out.colour[1] = source.color.green_multiplier;
  out.colour[2] = source.color.blue_multiplier;
  out.colour[3] = source.color.alpha_multiplier;
  out.colour[4] = static_cast<double>(source.color.red_offset);
  out.colour[5] = static_cast<double>(source.color.green_offset);
  out.colour[6] = static_cast<double>(source.color.blue_offset);
  out.colour[7] = static_cast<double>(source.color.alpha_offset);
  out.run_count = source.runs.size();
  out.filter_count = source.filter_count;
}

}  // namespace

extern "C" {

radicle_status radicle_runtime_open(const radicle_runtime_host* host,
                                    radicle_runtime** out_runtime) {
  clear_error();
  if (out_runtime) *out_runtime = nullptr;
  if (out_runtime == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_runtime is required");
  return guarded("unknown exception while opening a runtime", [&] {
    auto runtime = std::make_unique<radicle_runtime>();
    if (host != nullptr) install_host(*runtime, *host);
    const fo4::avm2::RuntimeLoad link = runtime->runtime.link();
    if (!link.ok()) {
      return fail(RADICLE_STATUS_INTERNAL_ERROR, link.message.c_str());
    }
    *out_runtime = runtime.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_runtime_close(radicle_runtime* runtime) { delete runtime; }

radicle_status radicle_runtime_open_movie(radicle_runtime* runtime, const uint8_t* bytes,
                                          size_t size, radicle_movie** out_movie) {
  clear_error();
  if (out_movie) *out_movie = nullptr;
  if (runtime == nullptr || out_movie == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "runtime and out_movie are required");
  }
  return guarded("unknown exception while opening a movie",
                 [&] { return open_movie_bytes(*runtime, bytes, size, out_movie); });
}

radicle_status radicle_runtime_open_movie_from_ba2(radicle_runtime* runtime, const radicle_ba2* ba2,
                                                   const char* path, radicle_movie** out_movie) {
  clear_error();
  if (out_movie) *out_movie = nullptr;
  if (runtime == nullptr || ba2 == nullptr || path == nullptr || out_movie == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "runtime, ba2, path and out_movie are required");
  }
  return guarded("unknown exception while opening an archived movie", [&] {
    size_t index = 0;
    const radicle_status found = radicle_ba2_find(ba2, path, &index);
    if (found != RADICLE_STATUS_OK) return fail(RADICLE_STATUS_NOT_FOUND, "the archive has no such entry");
    std::uint8_t* bytes = nullptr;
    size_t size = 0;
    const radicle_status extracted = radicle_ba2_extract(ba2, index, &bytes, &size);
    if (extracted != RADICLE_STATUS_OK) {
      return fail(RADICLE_STATUS_PARSE_ERROR, "the archive entry could not be extracted");
    }
    const radicle_status result = open_movie_bytes(*runtime, bytes, size, out_movie);
    radicle_free_bytes(bytes);
    return result;
  });
}

void radicle_movie_close(radicle_movie* movie) {
  if (movie != nullptr) movie->movie->release_fonts();
  delete movie;
}

radicle_status radicle_movie_advance(radicle_movie* movie, double dt_seconds) {
  clear_error();
  if (movie == nullptr || movie->movie == nullptr || movie->owner == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "movie is required");
  }
  if (!(dt_seconds >= 0.0)) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "dt_seconds must be nonnegative");
  return guarded("unknown exception while advancing a movie", [&] {
    movie->movie->advance(movie->owner->interpreter, dt_seconds);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_movie_input(radicle_movie* movie, const radicle_runtime_input* input) {
  clear_error();
  if (movie == nullptr || movie->movie == nullptr || movie->owner == nullptr || input == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "movie and input are required");
  }
  if (input->kind > RADICLE_INPUT_WHEEL) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "input kind is out of range");
  }
  return guarded("unknown exception while applying input", [&] {
    fo4::avm2::apply_input(*movie->movie, movie->owner->interpreter, to_replay(*input));
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_movie_snapshot(radicle_movie* movie, radicle_runtime_snapshot** out_snapshot) {
  clear_error();
  if (out_snapshot) *out_snapshot = nullptr;
  if (movie == nullptr || movie->movie == nullptr || out_snapshot == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "movie and out_snapshot are required");
  }
  return guarded("unknown exception while taking a snapshot", [&] {
    auto snapshot = std::make_unique<radicle_runtime_snapshot>();
    snapshot->frame = movie->movie->take_snapshot();
    snapshot->runs.resize(snapshot->frame.entries.size());
    snapshot->glyphs.resize(snapshot->frame.entries.size());
    for (std::size_t index = 0; index < snapshot->frame.entries.size(); ++index) {
      fill_runs(snapshot->frame.entries[index], *snapshot, index);
    }
    *out_snapshot = snapshot.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_runtime_snapshot_destroy(radicle_runtime_snapshot* snapshot) { delete snapshot; }

size_t radicle_runtime_snapshot_entry_count(const radicle_runtime_snapshot* snapshot) {
  return snapshot == nullptr ? 0 : snapshot->frame.entries.size();
}

radicle_status radicle_runtime_snapshot_entry_at(const radicle_runtime_snapshot* snapshot,
                                                 size_t index, radicle_runtime_entry* out_entry) {
  clear_error();
  if (out_entry) *out_entry = {};
  if (snapshot == nullptr || out_entry == nullptr || index >= snapshot->frame.entries.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the snapshot entry index is out of range");
  }
  fill_entry(snapshot->frame.entries[index], *out_entry);
  out_entry->runs = snapshot->runs[index].empty() ? nullptr : snapshot->runs[index].data();
  out_entry->run_count = snapshot->runs[index].size();
  return RADICLE_STATUS_OK;
}

size_t radicle_runtime_snapshot_sound_count(const radicle_runtime_snapshot* snapshot) {
  return snapshot == nullptr ? 0 : snapshot->frame.sounds.size();
}

radicle_status radicle_runtime_snapshot_sound_at(const radicle_runtime_snapshot* snapshot,
                                                 size_t index, radicle_runtime_sound* out_sound) {
  clear_error();
  if (out_sound) *out_sound = {};
  if (snapshot == nullptr || out_sound == nullptr || index >= snapshot->frame.sounds.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the snapshot sound index is out of range");
  }
  const fo4::avm2::SnapshotSoundEvent& sound = snapshot->frame.sounds[index];
  out_sound->kind = static_cast<std::uint8_t>(sound.kind);
  out_sound->sound_id = sound.sound_id;
  out_sound->volume = sound.volume;
  out_sound->loops = sound.loops;
  return RADICLE_STATUS_OK;
}

uint64_t radicle_runtime_snapshot_hash(const radicle_runtime_snapshot* snapshot) {
  clear_error();
  return snapshot == nullptr ? 0 : fo4::avm2::snapshot_hash(snapshot->frame);
}

radicle_status radicle_movie_replay(radicle_movie* movie, const radicle_runtime_input* inputs,
                                    size_t input_count, double frame_dt_seconds,
                                    size_t frame_count, uint64_t* out_hashes, size_t hash_capacity,
                                    size_t* out_hash_count) {
  clear_error();
  if (out_hash_count) *out_hash_count = 0;
  if (movie == nullptr || movie->movie == nullptr || movie->owner == nullptr ||
      out_hash_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "movie and out_hash_count are required");
  }
  if (input_count != 0 && inputs == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "inputs are missing");
  }
  if (!(frame_dt_seconds >= 0.0)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "frame_dt_seconds must be nonnegative");
  }
  const std::size_t capacity = out_hashes == nullptr ? 0 : hash_capacity;
  if (capacity != 0 && out_hashes == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_hashes is missing");
  }
  return guarded("unknown exception while replaying", [&] {
    std::vector<fo4::avm2::ReplayInput> replay_inputs;
    replay_inputs.reserve(input_count);
    for (size_t index = 0; index < input_count; ++index) {
      if (inputs[index].kind > RADICLE_INPUT_WHEEL) {
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "an input kind is out of range");
      }
      replay_inputs.push_back(to_replay(inputs[index]));
    }
    const fo4::avm2::ReplayResult result = fo4::avm2::replay(
        *movie->movie, movie->owner->interpreter, replay_inputs, frame_dt_seconds, frame_count);
    const std::size_t written = result.frame_hashes.size() < capacity ? result.frame_hashes.size()
                                                                     : capacity;
    for (std::size_t index = 0; index < written; ++index) {
      out_hashes[index] = result.frame_hashes[index];
    }
    *out_hash_count = result.frame_hashes.size();
    return RADICLE_STATUS_OK;
  });
}

}  // extern "C"
