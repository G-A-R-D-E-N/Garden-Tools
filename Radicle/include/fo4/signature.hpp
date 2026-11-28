#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace fo4 {

class Signature {
public:
  constexpr Signature() noexcept = default;

  constexpr Signature(char a, char b, char c, char d) noexcept
      : bytes_{static_cast<char>(a), static_cast<char>(b), static_cast<char>(c),
               static_cast<char>(d)} {}

  static constexpr Signature from_bytes(std::uint32_t packed) noexcept {
    return Signature{
        static_cast<char>(packed & 0xFFu),
        static_cast<char>((packed >> 8) & 0xFFu),
        static_cast<char>((packed >> 16) & 0xFFu),
        static_cast<char>((packed >> 24) & 0xFFu),
    };
  }

  static constexpr Signature from_string(std::string_view text) {
    Signature s;
    if (text.size() >= 1) s.bytes_[0] = text[0];
    if (text.size() >= 2) s.bytes_[1] = text[1];
    if (text.size() >= 3) s.bytes_[2] = text[2];
    if (text.size() >= 4) s.bytes_[3] = text[3];
    return s;
  }

  constexpr std::uint32_t packed() const noexcept {
    return static_cast<std::uint8_t>(bytes_[0]) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes_[1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes_[2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes_[3])) << 24);
  }

  constexpr std::string_view view() const noexcept {
    return std::string_view{bytes_.data(), 4};
  }

  std::string to_string() const { return std::string{view()}; }

  constexpr const std::array<char, 4>& bytes() const noexcept { return bytes_; }

  friend constexpr bool operator==(const Signature&, const Signature&) = default;
  friend constexpr auto operator<=>(const Signature&, const Signature&) = default;

private:
  std::array<char, 4> bytes_{};
};

constexpr Signature sig(std::string_view text) { return Signature::from_string(text); }

inline constexpr Signature sig_tes4 = Signature::from_string("TES4");
inline constexpr Signature sig_grup = Signature::from_string("GRUP");
inline constexpr Signature sig_xxxx = Signature::from_string("XXXX");
inline constexpr Signature sig_hedr = Signature::from_string("HEDR");
inline constexpr Signature sig_cnam = Signature::from_string("CNAM");
inline constexpr Signature sig_snam = Signature::from_string("SNAM");
inline constexpr Signature sig_mast = Signature::from_string("MAST");
inline constexpr Signature sig_data = Signature::from_string("DATA");
inline constexpr Signature sig_onam = Signature::from_string("ONAM");
inline constexpr Signature sig_intv = Signature::from_string("INTV");
inline constexpr Signature sig_incc = Signature::from_string("INCC");
inline constexpr Signature sig_edid = Signature::from_string("EDID");
inline constexpr Signature sig_name = Signature::from_string("NAME");
inline constexpr Signature sig_kwda = Signature::from_string("KWDA");
inline constexpr Signature sig_ctda = Signature::from_string("CTDA");
inline constexpr Signature sig_xlcn = Signature::from_string("XLCN");
inline constexpr Signature sig_xown = Signature::from_string("XOWN");
inline constexpr Signature sig_xezn = Signature::from_string("XEZN");
inline constexpr Signature sig_ptrn = Signature::from_string("PTRN");
inline constexpr Signature sig_vmad = Signature::from_string("VMAD");
inline constexpr Signature sig_qust = Signature::from_string("QUST");
inline constexpr Signature sig_perk = Signature::from_string("PERK");
inline constexpr Signature sig_pack = Signature::from_string("PACK");
inline constexpr Signature sig_scen = Signature::from_string("SCEN");
inline constexpr Signature sig_info = Signature::from_string("INFO");
inline constexpr Signature sig_term = Signature::from_string("TERM");
inline constexpr Signature sig_cnto = Signature::from_string("CNTO");
inline constexpr Signature sig_lvlo = Signature::from_string("LVLO");
inline constexpr Signature sig_xtel = Signature::from_string("XTEL");
inline constexpr Signature sig_xloc = Signature::from_string("XLOC");
inline constexpr Signature sig_xlkr = Signature::from_string("XLKR");
inline constexpr Signature sig_xesp = Signature::from_string("XESP");
inline constexpr Signature sig_coed = Signature::from_string("COED");
inline constexpr Signature sig_xndp = Signature::from_string("XNDP");
inline constexpr Signature sig_achr = Signature::from_string("ACHR");
inline constexpr Signature sig_alcc = Signature::from_string("ALCC");
inline constexpr Signature sig_alcs = Signature::from_string("ALCS");
inline constexpr Signature sig_aled = Signature::from_string("ALED");
inline constexpr Signature sig_alfa = Signature::from_string("ALFA");
inline constexpr Signature sig_alid = Signature::from_string("ALID");
inline constexpr Signature sig_alls = Signature::from_string("ALLS");
inline constexpr Signature sig_almi = Signature::from_string("ALMI");
inline constexpr Signature sig_alst = Signature::from_string("ALST");
inline constexpr Signature sig_anam = Signature::from_string("ANAM");
inline constexpr Signature sig_arma = Signature::from_string("ARMA");
inline constexpr Signature sig_armo = Signature::from_string("ARMO");
inline constexpr Signature sig_bnam = Signature::from_string("BNAM");
inline constexpr Signature sig_book = Signature::from_string("BOOK");
inline constexpr Signature sig_cell = Signature::from_string("CELL");
inline constexpr Signature sig_cis1 = Signature::from_string("CIS1");
inline constexpr Signature sig_cis2 = Signature::from_string("CIS2");
inline constexpr Signature sig_citc = Signature::from_string("CITC");
inline constexpr Signature sig_cobj = Signature::from_string("COBJ");
inline constexpr Signature sig_coct = Signature::from_string("COCT");
inline constexpr Signature sig_crva = Signature::from_string("CRVA");
inline constexpr Signature sig_desc = Signature::from_string("DESC");
inline constexpr Signature sig_dial = Signature::from_string("DIAL");
inline constexpr Signature sig_dlbr = Signature::from_string("DLBR");
inline constexpr Signature sig_dnam = Signature::from_string("DNAM");
inline constexpr Signature sig_eczn = Signature::from_string("ECZN");
inline constexpr Signature sig_efid = Signature::from_string("EFID");
inline constexpr Signature sig_efit = Signature::from_string("EFIT");
inline constexpr Signature sig_enam = Signature::from_string("ENAM");
inline constexpr Signature sig_epfd = Signature::from_string("EPFD");
inline constexpr Signature sig_epft = Signature::from_string("EPFT");
inline constexpr Signature sig_expl = Signature::from_string("EXPL");
inline constexpr Signature sig_fact = Signature::from_string("FACT");
inline constexpr Signature sig_fnam = Signature::from_string("FNAM");
inline constexpr Signature sig_full = Signature::from_string("FULL");
inline constexpr Signature sig_hazd = Signature::from_string("HAZD");
inline constexpr Signature sig_ico2 = Signature::from_string("ICO2");
inline constexpr Signature sig_icon = Signature::from_string("ICON");
inline constexpr Signature sig_inam = Signature::from_string("INAM");
inline constexpr Signature sig_indx = Signature::from_string("INDX");
inline constexpr Signature sig_knam = Signature::from_string("KNAM");
inline constexpr Signature sig_llct = Signature::from_string("LLCT");
inline constexpr Signature sig_lnam = Signature::from_string("LNAM");
inline constexpr Signature sig_lvld = Signature::from_string("LVLD");
inline constexpr Signature sig_lvlf = Signature::from_string("LVLF");
inline constexpr Signature sig_lvli = Signature::from_string("LVLI");
inline constexpr Signature sig_lvlm = Signature::from_string("LVLM");
inline constexpr Signature sig_lvln = Signature::from_string("LVLN");
inline constexpr Signature sig_lvsp = Signature::from_string("LVSP");
inline constexpr Signature sig_mgef = Signature::from_string("MGEF");
inline constexpr Signature sig_mic2 = Signature::from_string("MIC2");
inline constexpr Signature sig_mico = Signature::from_string("MICO");
inline constexpr Signature sig_mnam = Signature::from_string("MNAM");
inline constexpr Signature sig_mod2 = Signature::from_string("MOD2");
inline constexpr Signature sig_mod3 = Signature::from_string("MOD3");
inline constexpr Signature sig_mod4 = Signature::from_string("MOD4");
inline constexpr Signature sig_mod5 = Signature::from_string("MOD5");
inline constexpr Signature sig_modl = Signature::from_string("MODL");
inline constexpr Signature sig_modt = Signature::from_string("MODT");
inline constexpr Signature sig_nam0 = Signature::from_string("NAM0");
inline constexpr Signature sig_nam1 = Signature::from_string("NAM1");
inline constexpr Signature sig_nam2 = Signature::from_string("NAM2");
inline constexpr Signature sig_nam3 = Signature::from_string("NAM3");
inline constexpr Signature sig_nam4 = Signature::from_string("NAM4");
inline constexpr Signature sig_navi = Signature::from_string("NAVI");
inline constexpr Signature sig_navm = Signature::from_string("NAVM");
inline constexpr Signature sig_nnam = Signature::from_string("NNAM");
inline constexpr Signature sig_nvmi = Signature::from_string("NVMI");
inline constexpr Signature sig_nvnm = Signature::from_string("NVNM");
inline constexpr Signature sig_nvpp = Signature::from_string("NVPP");
inline constexpr Signature sig_obnd = Signature::from_string("OBND");
inline constexpr Signature sig_obts = Signature::from_string("OBTS");
inline constexpr Signature sig_omod = Signature::from_string("OMOD");
inline constexpr Signature sig_otft = Signature::from_string("OTFT");
inline constexpr Signature sig_plvd = Signature::from_string("PLVD");
inline constexpr Signature sig_pnam = Signature::from_string("PNAM");
inline constexpr Signature sig_prkc = Signature::from_string("PRKC");
inline constexpr Signature sig_prke = Signature::from_string("PRKE");
inline constexpr Signature sig_proj = Signature::from_string("PROJ");
inline constexpr Signature sig_qnam = Signature::from_string("QNAM");
inline constexpr Signature sig_qobj = Signature::from_string("QOBJ");
inline constexpr Signature sig_qsta = Signature::from_string("QSTA");
inline constexpr Signature sig_race = Signature::from_string("RACE");
inline constexpr Signature sig_refr = Signature::from_string("REFR");
inline constexpr Signature sig_rnam = Signature::from_string("RNAM");
inline constexpr Signature sig_scol = Signature::from_string("SCOL");
inline constexpr Signature sig_spel = Signature::from_string("SPEL");
inline constexpr Signature sig_sraf = Signature::from_string("SRAF");
inline constexpr Signature sig_stat = Signature::from_string("STAT");
inline constexpr Signature sig_tifc = Signature::from_string("TIFC");
inline constexpr Signature sig_tnam = Signature::from_string("TNAM");
inline constexpr Signature sig_tplt = Signature::from_string("TPLT");
inline constexpr Signature sig_tpta = Signature::from_string("TPTA");
inline constexpr Signature sig_trda = Signature::from_string("TRDA");
inline constexpr Signature sig_unam = Signature::from_string("UNAM");
inline constexpr Signature sig_vnam = Signature::from_string("VNAM");
inline constexpr Signature sig_vtck = Signature::from_string("VTCK");
inline constexpr Signature sig_wnam = Signature::from_string("WNAM");
inline constexpr Signature sig_wrld = Signature::from_string("WRLD");
inline constexpr Signature sig_xclc = Signature::from_string("XCLC");
inline constexpr Signature sig_xnam = Signature::from_string("XNAM");
inline constexpr Signature sig_znam = Signature::from_string("ZNAM");
inline constexpr Signature sig_acbs = Signature::from_string("ACBS");
inline constexpr Signature sig_acec = Signature::from_string("ACEC");
inline constexpr Signature sig_acti = Signature::from_string("ACTI");
inline constexpr Signature sig_aidt = Signature::from_string("AIDT");
inline constexpr Signature sig_alca = Signature::from_string("ALCA");
inline constexpr Signature sig_alch = Signature::from_string("ALCH");
inline constexpr Signature sig_alcl = Signature::from_string("ALCL");
inline constexpr Signature sig_alco = Signature::from_string("ALCO");
inline constexpr Signature sig_aldi = Signature::from_string("ALDI");
inline constexpr Signature sig_aldn = Signature::from_string("ALDN");
inline constexpr Signature sig_alea = Signature::from_string("ALEA");
inline constexpr Signature sig_aleq = Signature::from_string("ALEQ");
inline constexpr Signature sig_alfc = Signature::from_string("ALFC");
inline constexpr Signature sig_alfd = Signature::from_string("ALFD");
inline constexpr Signature sig_alfe = Signature::from_string("ALFE");
inline constexpr Signature sig_alfi = Signature::from_string("ALFI");
inline constexpr Signature sig_alfl = Signature::from_string("ALFL");
inline constexpr Signature sig_alfr = Signature::from_string("ALFR");
inline constexpr Signature sig_alfv = Signature::from_string("ALFV");
inline constexpr Signature sig_alla = Signature::from_string("ALLA");
inline constexpr Signature sig_alna = Signature::from_string("ALNA");
inline constexpr Signature sig_alnt = Signature::from_string("ALNT");
inline constexpr Signature sig_alpc = Signature::from_string("ALPC");
inline constexpr Signature sig_alrt = Signature::from_string("ALRT");
inline constexpr Signature sig_alsp = Signature::from_string("ALSP");
inline constexpr Signature sig_alua = Signature::from_string("ALUA");
inline constexpr Signature sig_avif = Signature::from_string("AVIF");
inline constexpr Signature sig_bod2 = Signature::from_string("BOD2");
inline constexpr Signature sig_bsiz = Signature::from_string("BSIZ");
inline constexpr Signature sig_btxt = Signature::from_string("BTXT");
inline constexpr Signature sig_cdix = Signature::from_string("CDIX");
inline constexpr Signature sig_clmt = Signature::from_string("CLMT");
inline constexpr Signature sig_cmpo = Signature::from_string("CMPO");
inline constexpr Signature sig_coll = Signature::from_string("COLL");
inline constexpr Signature sig_crgr = Signature::from_string("CRGR");
inline constexpr Signature sig_crif = Signature::from_string("CRIF");
inline constexpr Signature sig_cusd = Signature::from_string("CUSD");
inline constexpr Signature sig_cvpa = Signature::from_string("CVPA");
inline constexpr Signature sig_dalc = Signature::from_string("DALC");
inline constexpr Signature sig_dama = Signature::from_string("DAMA");
inline constexpr Signature sig_damc = Signature::from_string("DAMC");
inline constexpr Signature sig_dele = Signature::from_string("DELE");
inline constexpr Signature sig_dest = Signature::from_string("DEST");
inline constexpr Signature sig_dfob = Signature::from_string("DFOB");
inline constexpr Signature sig_dmdc = Signature::from_string("DMDC");
inline constexpr Signature sig_dmdl = Signature::from_string("DMDL");
inline constexpr Signature sig_dmds = Signature::from_string("DMDS");
inline constexpr Signature sig_dmdt = Signature::from_string("DMDT");
inline constexpr Signature sig_dobj = Signature::from_string("DOBJ");
inline constexpr Signature sig_dodt = Signature::from_string("DODT");
inline constexpr Signature sig_doft = Signature::from_string("DOFT");
inline constexpr Signature sig_dsta = Signature::from_string("DSTA");
inline constexpr Signature sig_dstd = Signature::from_string("DSTD");
inline constexpr Signature sig_dstf = Signature::from_string("DSTF");
inline constexpr Signature sig_ecor = Signature::from_string("ECOR");
inline constexpr Signature sig_ench = Signature::from_string("ENCH");
inline constexpr Signature sig_epf2 = Signature::from_string("EPF2");
inline constexpr Signature sig_epf3 = Signature::from_string("EPF3");
inline constexpr Signature sig_epfb = Signature::from_string("EPFB");
inline constexpr Signature sig_equp = Signature::from_string("EQUP");
inline constexpr Signature sig_eyes = Signature::from_string("EYES");
inline constexpr Signature sig_flst = Signature::from_string("FLST");
inline constexpr Signature sig_fstp = Signature::from_string("FSTP");
inline constexpr Signature sig_fsts = Signature::from_string("FSTS");
inline constexpr Signature sig_furn = Signature::from_string("FURN");
inline constexpr Signature sig_fvpa = Signature::from_string("FVPA");
inline constexpr Signature sig_gmst = Signature::from_string("GMST");
inline constexpr Signature sig_gnam = Signature::from_string("GNAM");
inline constexpr Signature sig_gree = Signature::from_string("GREE");
inline constexpr Signature sig_gwor = Signature::from_string("GWOR");
inline constexpr Signature sig_hclf = Signature::from_string("HCLF");
inline constexpr Signature sig_htid = Signature::from_string("HTID");
inline constexpr Signature sig_imsp = Signature::from_string("IMSP");
inline constexpr Signature sig_ingr = Signature::from_string("INGR");
inline constexpr Signature sig_iovr = Signature::from_string("IOVR");
inline constexpr Signature sig_ipds = Signature::from_string("IPDS");
inline constexpr Signature sig_isiz = Signature::from_string("ISIZ");
inline constexpr Signature sig_itid = Signature::from_string("ITID");
inline constexpr Signature sig_itmc = Signature::from_string("ITMC");
inline constexpr Signature sig_itme = Signature::from_string("ITME");
inline constexpr Signature sig_itms = Signature::from_string("ITMS");
inline constexpr Signature sig_itxt = Signature::from_string("ITXT");
inline constexpr Signature sig_jnam = Signature::from_string("JNAM");
inline constexpr Signature sig_ksiz = Signature::from_string("KSIZ");
inline constexpr Signature sig_kywd = Signature::from_string("KYWD");
inline constexpr Signature sig_land = Signature::from_string("LAND");
inline constexpr Signature sig_lcec = Signature::from_string("LCEC");
inline constexpr Signature sig_lctn = Signature::from_string("LCTN");
inline constexpr Signature sig_lscr = Signature::from_string("LSCR");
inline constexpr Signature sig_ltmp = Signature::from_string("LTMP");
inline constexpr Signature sig_lvlg = Signature::from_string("LVLG");
inline constexpr Signature sig_lvsg = Signature::from_string("LVSG");
inline constexpr Signature sig_misc = Signature::from_string("MISC");
inline constexpr Signature sig_modc = Signature::from_string("MODC");
inline constexpr Signature sig_modf = Signature::from_string("MODF");
inline constexpr Signature sig_modq = Signature::from_string("MODQ");
inline constexpr Signature sig_mods = Signature::from_string("MODS");
inline constexpr Signature sig_musc = Signature::from_string("MUSC");
inline constexpr Signature sig_nam5 = Signature::from_string("NAM5");
inline constexpr Signature sig_nam6 = Signature::from_string("NAM6");
inline constexpr Signature sig_nam7 = Signature::from_string("NAM7");
inline constexpr Signature sig_nam8 = Signature::from_string("NAM8");
inline constexpr Signature sig_nam9 = Signature::from_string("NAM9");
inline constexpr Signature sig_next = Signature::from_string("NEXT");
inline constexpr Signature sig_note = Signature::from_string("NOTE");
inline constexpr Signature sig_npc_ = Signature::from_string("NPC_");
inline constexpr Signature sig_nver = Signature::from_string("NVER");
inline constexpr Signature sig_nvsi = Signature::from_string("NVSI");
inline constexpr Signature sig_obte = Signature::from_string("OBTE");
inline constexpr Signature sig_ocor = Signature::from_string("OCOR");
inline constexpr Signature sig_ofst = Signature::from_string("OFST");
inline constexpr Signature sig_ovis = Signature::from_string("OVIS");
inline constexpr Signature sig_pcmb = Signature::from_string("PCMB");
inline constexpr Signature sig_prkf = Signature::from_string("PRKF");
inline constexpr Signature sig_prkr = Signature::from_string("PRKR");
inline constexpr Signature sig_prkz = Signature::from_string("PRKZ");
inline constexpr Signature sig_qsdt = Signature::from_string("QSDT");
inline constexpr Signature sig_rcec = Signature::from_string("RCEC");
inline constexpr Signature sig_regn = Signature::from_string("REGN");
inline constexpr Signature sig_rept = Signature::from_string("REPT");
inline constexpr Signature sig_rfgp = Signature::from_string("RFGP");
inline constexpr Signature sig_scco = Signature::from_string("SCCO");
inline constexpr Signature sig_sndr = Signature::from_string("SNDR");
inline constexpr Signature sig_spgd = Signature::from_string("SPGD");
inline constexpr Signature sig_splo = Signature::from_string("SPLO");
inline constexpr Signature sig_spor = Signature::from_string("SPOR");
inline constexpr Signature sig_stag = Signature::from_string("STAG");
inline constexpr Signature sig_stop = Signature::from_string("STOP");
inline constexpr Signature sig_tiqs = Signature::from_string("TIQS");
inline constexpr Signature sig_tpic = Signature::from_string("TPIC");
inline constexpr Signature sig_tree = Signature::from_string("TREE");
inline constexpr Signature sig_tsce = Signature::from_string("TSCE");
inline constexpr Signature sig_tx00 = Signature::from_string("TX00");
inline constexpr Signature sig_txst = Signature::from_string("TXST");
inline constexpr Signature sig_venc = Signature::from_string("VENC");
inline constexpr Signature sig_vend = Signature::from_string("VEND");
inline constexpr Signature sig_venv = Signature::from_string("VENV");
inline constexpr Signature sig_vhgt = Signature::from_string("VHGT");
inline constexpr Signature sig_visi = Signature::from_string("VISI");
inline constexpr Signature sig_vnml = Signature::from_string("VNML");
inline constexpr Signature sig_vtyp = Signature::from_string("VTYP");
inline constexpr Signature sig_weap = Signature::from_string("WEAP");
inline constexpr Signature sig_wgdr = Signature::from_string("WGDR");
inline constexpr Signature sig_wlst = Signature::from_string("WLST");
inline constexpr Signature sig_wthr = Signature::from_string("WTHR");
inline constexpr Signature sig_wzmd = Signature::from_string("WZMD");
inline constexpr Signature sig_xcas = Signature::from_string("XCAS");
inline constexpr Signature sig_xccm = Signature::from_string("XCCM");
inline constexpr Signature sig_xcim = Signature::from_string("XCIM");
inline constexpr Signature sig_xcll = Signature::from_string("XCLL");
inline constexpr Signature sig_xclr = Signature::from_string("XCLR");
inline constexpr Signature sig_xclw = Signature::from_string("XCLW");
inline constexpr Signature sig_xcmo = Signature::from_string("XCMO");
inline constexpr Signature sig_xcnt = Signature::from_string("XCNT");
inline constexpr Signature sig_xcri = Signature::from_string("XCRI");
inline constexpr Signature sig_xcwt = Signature::from_string("XCWT");
inline constexpr Signature sig_xgdr = Signature::from_string("XGDR");
inline constexpr Signature sig_xill = Signature::from_string("XILL");
inline constexpr Signature sig_xilw = Signature::from_string("XILW");
inline constexpr Signature sig_xmsp = Signature::from_string("XMSP");
inline constexpr Signature sig_xpri = Signature::from_string("XPRI");
inline constexpr Signature sig_xrfg = Signature::from_string("XRFG");
inline constexpr Signature sig_xrnk = Signature::from_string("XRNK");
inline constexpr Signature sig_xscl = Signature::from_string("XSCL");
inline constexpr Signature sig_xwcn = Signature::from_string("XWCN");
inline constexpr Signature sig_xwcu = Signature::from_string("XWCU");
inline constexpr Signature sig_xwem = Signature::from_string("XWEM");

}  // namespace fo4
