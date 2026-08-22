#include "f4lod/btr_geometry.hpp"

#include "f4lod/nif.hpp"

#include <meshoptimizer.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

namespace f4lod {
namespace {

class Cursor {
public:
    Cursor(const std::vector<unsigned char>& data, std::size_t begin, std::size_t end)
        : data_(data), pos_(begin), end_(end <= data.size() ? end : data.size()) {}
    [[nodiscard]] std::size_t remaining() const noexcept { return pos_ <= end_ ? end_ - pos_ : 0; }
    bool u8(std::uint8_t& v) { if (remaining()<1) return false; v=data_[pos_++]; return true; }
    bool u16(std::uint16_t& v) { if (remaining()<2) return false; v=static_cast<std::uint16_t>(data_[pos_])|(static_cast<std::uint16_t>(data_[pos_+1])<<8U); pos_+=2; return true; }
    bool u32(std::uint32_t& v) { if (remaining()<4) return false; v=static_cast<std::uint32_t>(data_[pos_])|(static_cast<std::uint32_t>(data_[pos_+1])<<8U)|(static_cast<std::uint32_t>(data_[pos_+2])<<16U)|(static_cast<std::uint32_t>(data_[pos_+3])<<24U); pos_+=4; return true; }
    bool i32(std::int32_t& v) { std::uint32_t bits=0; if(!u32(bits)) return false; std::memcpy(&v,&bits,sizeof(v)); return true; }
    bool f32(float& v) { std::uint32_t bits=0; if(!u32(bits)) return false; std::memcpy(&v,&bits,sizeof(v)); return true; }
    bool skip(std::size_t n) { if(n>remaining()) return false; pos_+=n; return true; }
private:
    const std::vector<unsigned char>& data_; std::size_t pos_{}; std::size_t end_{};
};

std::vector<unsigned char> read_all(const std::filesystem::path& path, std::string& error) {
    std::ifstream input(path,std::ios::binary);
    if(!input){error="Could not open BTR file.";return{};}
    input.seekg(0,std::ios::end); const auto end=input.tellg();
    if(end<0){error="Could not determine BTR file size.";return{};}
    const auto size64=static_cast<std::uint64_t>(end);
    if(size64>static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())){error="BTR file is too large for this process.";return{};}
    input.seekg(0,std::ios::beg); std::vector<unsigned char> data(static_cast<std::size_t>(size64));
    if(!data.empty()){input.read(reinterpret_cast<char*>(data.data()),static_cast<std::streamsize>(data.size())); if(input.gcount()!=static_cast<std::streamsize>(data.size())){error="BTR file is truncated.";return{};}}
    return data;
}

std::uint32_t vertex_stride(std::uint16_t flags) noexcept {
    std::uint32_t size=0;
    if(flags&16U) size+=(flags&16384U)?16U:8U;
    if(flags&32U) size+=4U; if(flags&128U) size+=4U; if(flags&256U) size+=4U;
    if(flags&512U) size+=4U; if(flags&1024U) size+=12U; if(flags&4096U) size+=4U;
    return size;
}

bool parse_fo4_avobject_prefix(Cursor& c,BtrGeometryInspection& out) {
    if(!c.i32(out.name_index)) return false;
    std::uint32_t extra=0; if(!c.u32(extra)||extra>1'000'000U||!c.skip(static_cast<std::size_t>(extra)*4U)) return false;
    std::int32_t controller=-1; if(!c.i32(controller)||!c.u16(out.av_flags)||!c.u16(out.av_flags2)) return false;
    for(float& v:out.translation) if(!c.f32(v)) return false;
    for(float& v:out.rotation) if(!c.f32(v)) return false;
    if(!c.f32(out.scale)) return false;
    std::int32_t collision=-1; return c.i32(collision);
}

void include_position(BtrGeometryInspection& out,float x,float y,float z) {
    if(!out.has_position_range){out.position_min[0]=out.position_max[0]=x;out.position_min[1]=out.position_max[1]=y;out.position_min[2]=out.position_max[2]=z;out.has_position_range=true;return;}
    out.position_min[0]=std::min(out.position_min[0],x);out.position_max[0]=std::max(out.position_max[0],x);
    out.position_min[1]=std::min(out.position_min[1],y);out.position_max[1]=std::max(out.position_max[1],y);
    out.position_min[2]=std::min(out.position_min[2],z);out.position_max[2]=std::max(out.position_max[2],z);
}
void include_uv(BtrGeometryInspection& out,float u,float v) {
    if(!out.has_uv_range){out.uv_min[0]=out.uv_max[0]=u;out.uv_min[1]=out.uv_max[1]=v;out.has_uv_range=true;return;}
    out.uv_min[0]=std::min(out.uv_min[0],u);out.uv_max[0]=std::max(out.uv_max[0],u);
    out.uv_min[1]=std::min(out.uv_min[1],v);out.uv_max[1]=std::max(out.uv_max[1],v);
}

bool parse_vertices(Cursor& c,BtrGeometryInspection& out) {
    const bool full=(out.vertex_flags&16384U)!=0U; out.full_precision_positions=full;
    for(std::uint32_t i=0;i<out.vertex_count;++i){
        if(out.vertex_flags&16U){
            float x=0,y=0,z=0;
            if(full){float bitangent=0;if(!c.f32(x)||!c.f32(y)||!c.f32(z)||!c.f32(bitangent))return false;}
            else{std::uint16_t hx=0,hy=0,hz=0,hb=0;if(!c.u16(hx)||!c.u16(hy)||!c.u16(hz)||!c.u16(hb))return false;x=meshopt_dequantizeHalf(hx);y=meshopt_dequantizeHalf(hy);z=meshopt_dequantizeHalf(hz);}
            include_position(out,x,y,z);
        }
        if(out.vertex_flags&32U){std::uint16_t hu=0,hv=0;if(!c.u16(hu)||!c.u16(hv))return false;include_uv(out,meshopt_dequantizeHalf(hu),meshopt_dequantizeHalf(hv));}
        if((out.vertex_flags&128U)&&!c.skip(4U)) return false;
        if((out.vertex_flags&256U)&&!c.skip(4U)) return false;
        if((out.vertex_flags&512U)&&!c.skip(4U)) return false;
        if((out.vertex_flags&1024U)&&!c.skip(12U)) return false;
        if((out.vertex_flags&4096U)&&!c.skip(4U)) return false;
    }
    return c.skip(static_cast<std::size_t>(out.triangle_count)*6U);
}

} // namespace

BtrGeometryInspection inspect_fallout4_btr_geometry(const std::filesystem::path& path,std::int32_t block_index) {
    BtrGeometryInspection result;
    const auto nif=inspect_nif(path);
    if(!nif.valid){result.error=nif.error;return result;}
    if(!nif.is_fallout4()){result.error="Geometry is not in a Fallout 4 12/130 NIF stream.";return result;}
    if(block_index<0||static_cast<std::size_t>(block_index)>=nif.blocks.size()){result.error="Terrain geometry block index is out of range.";return result;}
    const auto& block=nif.blocks[static_cast<std::size_t>(block_index)]; result.type=block.type;
    if(result.type!="BSTriShape"&&result.type!="BSSubIndexTriShape"){result.error="Terrain geometry is not BSTriShape or BSSubIndexTriShape.";return result;}
    std::string file_error;const auto data=read_all(path,file_error);if(!file_error.empty()){result.error=std::move(file_error);return result;}
    if(block.offset+block.size>data.size()){result.error="Terrain geometry block extends past the BTR file.";return result;}
    Cursor c(data,static_cast<std::size_t>(block.offset),static_cast<std::size_t>(block.offset+block.size));
    if(!parse_fo4_avobject_prefix(c,result)){result.error="Terrain BSTriShape is truncated in NiAVObject fields.";return result;}
    for(float& v:result.center)if(!c.f32(v)){result.error="Terrain BSTriShape is truncated at bounds center.";return result;}
    if(!c.f32(result.radius)||!c.i32(result.skin_instance)||!c.i32(result.shader_properties[0])||!c.i32(result.shader_properties[1])||
       !c.u8(result.vertex_size_words)||!c.u8(result.float_size_words)||!c.u8(result.vertex_flag3)||!c.u8(result.vertex_flag4)||
       !c.u8(result.vertex_flag5)||!c.u16(result.vertex_flags)||!c.u8(result.vertex_flag8)||!c.u32(result.triangle_count)||
       !c.u16(result.vertex_count)||!c.u32(result.data_size)){result.error="Terrain BSTriShape header is truncated.";return result;}
    result.vertex_stride=vertex_stride(result.vertex_flags);
    result.expected_geometry_bytes=static_cast<std::uint64_t>(result.vertex_count)*result.vertex_stride+static_cast<std::uint64_t>(result.triangle_count)*6ULL;
    if(result.vertex_count!=0&&(result.vertex_flags&16U)==0U){result.error="Terrain BSTriShape has vertices but no position flag.";return result;}
    if(result.expected_geometry_bytes!=result.data_size){result.error="Terrain BSTriShape dataSize does not match its vertex/index descriptor.";return result;}
    if(result.data_size>c.remaining()||!parse_vertices(c,result)){result.error="Terrain BSTriShape geometry payload is truncated.";return result;}
    if(result.type=="BSSubIndexTriShape"){
        if(!c.u32(result.subindex_triangle_count)||!c.u32(result.subindex_a)||!c.u32(result.subindex_b)){result.error="BSSubIndexTriShape subindex header is truncated.";return result;}
        if(result.subindex_a>1'000'000U||static_cast<std::uint64_t>(result.subindex_a)*16ULL>c.remaining()){result.error="BSSubIndexTriShape subindex table is invalid.";return result;}
        result.subindex_segments.reserve(result.subindex_a);
        for(std::uint32_t i=0;i<result.subindex_a;++i){BtrSubIndexSegment s;if(!c.u32(s.triangle_offset)||!c.u32(s.triangle_count)||!c.u32(s.unknown_hash)||!c.u32(s.num_segments)){result.error="BSSubIndexTriShape subindex entry is truncated.";return result;}result.subindex_segments.push_back(s);}
    }
    if(c.remaining()!=0){result.error="Terrain geometry block contains unparsed trailing bytes.";return result;}
    result.valid=true;return result;
}

} // namespace f4lod
