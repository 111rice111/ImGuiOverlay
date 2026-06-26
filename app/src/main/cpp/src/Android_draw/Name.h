#pragma once
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
static const char *getplayer(const char *类名) {
  std::string_view sv(类名);
  if (sv.find("detective") != std::string_view::npos)
    return "[侦探]";
  if (sv.find("npc_deluosi_dress_ghost") != std::string_view::npos)
    return "[联觉记者]";
  if (sv.find("h55_survivor_w_xiangshuishi") != std::string_view::npos)
    return "[调香师]";
  if (sv.find("h55_survivor_w_bdz") != std::string_view::npos)
    return "[空军]";
  if (sv.find("h55_survivor_m_yxz_bo") != std::string_view::npos)
    return "[佣兵]";
  if (sv.find("dm65_survivor_m_bo") != std::string_view::npos)
    return "[幸运儿]";
  if (sv.find("h55_survivor_m_qiutu") != std::string_view::npos)
    return "[囚徒]";
  if (sv.find("dm65_survivor_w_yiyaoshi") != std::string_view::npos)
    return "[医生]";
  if (sv.find("h55_survivor_m_xcz") != std::string_view::npos)
    return "[冒险家]";
  if (sv.find("h55_survivor_w_cp") != std::string_view::npos)
    return "[心理学家]";
  if (sv.find("h55_survivor_m_cp") != std::string_view::npos)
    return "[病患]";
  if (sv.find("h55_survivor_w_hlz") != std::string_view::npos)
    return "[园丁]";
  if (sv.find("h55_survivor_m_zbs") != std::string_view::npos)
    return "[先知]";
  if (sv.find("h55_survivor_w_fxt") != std::string_view::npos ||
      sv.find("h55_survivor_tqz") != std::string_view::npos)
    return "[机械师]";
  if (sv.find("h55_survivor_puppet") != std::string_view::npos)
    return "[儿子]";
  if (sv.find("h55_survivor_m_shoumu") != std::string_view::npos)
    return "[守墓人]";
  if (sv.find("h55_prop_tieqiao") != std::string_view::npos)
    return "[守墓人]";
    if (sv.find("h55_prop_tieqiao.gim") != std::string_view::npos)
        return "[守墓人]";
    if (sv.find("prop") != std::string_view::npos)
        return "[守墓人]";
  if (sv.find("h55_survivor_m_ydy") != std::string_view::npos)
    return "[前锋]";
  if (sv.find("h55_survivor_w_wht") != std::string_view::npos)
    return "[舞女]";
  if (sv.find("h55_survivor_m_niuzai") != std::string_view::npos)
    return "[牛仔]";
  if (sv.find("h55_survivor_m_rls") != std::string_view::npos)
    return "[入殓师]";
  if (sv.find("h55_survivor_w_jyz") != std::string_view::npos)
    return "[盲女]";
  if (sv.find("h55_survivor_m_yc") != std::string_view::npos)
    return "[邮差]";
  if (sv.find("h55_survivor_w_jisi") != std::string_view::npos)
    return "[祭司]";
  if (sv.find("h55_survivor_m_ldz") != std::string_view::npos)
    return "[魔术师]";
  if (sv.find("survivor_girl") != std::string_view::npos)
    return "[小女孩]";
  if (sv.find("h55_survivor_w_zhoushu") != std::string_view::npos)
    return "[咒术师]";
  if (sv.find("h55_survivor_m_kantan") != std::string_view::npos)
    return "[勘探员]";
  if (sv.find("h55_survivor_m_yeren") != std::string_view::npos)
    return "[野人]";
  if (sv.find("h55_survivor_m_zaji") != std::string_view::npos)
    return "[杂技演员]";
  if (sv.find("h55_survivor_m_dafu") != std::string_view::npos)
    return "[大副]";
  if (sv.find("h55_survivor_w_tiaojiu") != std::string_view::npos)
    return "[调酒师]";
  if (sv.find("h55_survivor_w_kunchong") != std::string_view::npos)
    return "[昆虫学家]";
  if (sv.find("h55_survivor_m_artist") != std::string_view::npos)
    return "[画家]";
  if (sv.find("h55_survivor_m_jiqiu") != std::string_view::npos)
    return "[击球手]";
  if (sv.find("h55_survivor_w_shangren") != std::string_view::npos)
    return "[玩具商]";
  if (sv.find("h55_survivor_m_bzt") != std::string_view::npos)
    return "[小说家]";
  if (sv.find("h55_survivor_m_yinyue") != std::string_view::npos)
    return "[作曲家]";
  if (sv.find("h55_survivor_m_ttds") != std::string_view::npos)
    return "[逃脱大师]";
  if (sv.find("h55_survivor_w_hds") != std::string_view::npos)
    return "[幻灯师]";
  if (sv.find("h55_survivor_w_gjs") != std::string_view::npos)
    return "[弓箭手]";
  if (sv.find("h55_survivor_w_qx") != std::string_view::npos)
    return "[气象学家]";
  if (sv.find("h55_survivor_m_spjk") != std::string_view::npos)
    return "[哭泣小丑]";
  if (sv.find("h55_survivor_w_gd") != std::string_view::npos)
    return "[古董商]";
  if (sv.find("h55_survivor_m_niexi") != std::string_view::npos)
    return "[教授]";
  if (sv.find("h55_survivor_m_fxj") != std::string_view::npos)
    return "[飞行家]";
  if (sv.find("h55_survivor_w_deluosi") != std::string_view::npos)
    return "[记者]";
  if (sv.find("h55_survivor_m_muou") != std::string_view::npos)
    return "[木偶师]";
  if (sv.find("h55_survivor_m_it") != std::string_view::npos)
    return "[律师]";
  if (sv.find("h55_survivor_m_qd") != std::string_view::npos)
    return "[慈善家]";
  if (sv.find("h55_survivor_w_ll") != std::string_view::npos)
    return "[拉拉队员]";
  if (sv.find("survivor_w_fl") != std::string_view::npos)
    return "[法罗女士]";
  if (sv.find("h55_survivor_m_xf") != std::string_view::npos)
    return "[火灾调查员]";
  if (sv.find("h55_survivor_m_dxzh") != std::string_view::npos)
    return "[骑士]";
  if (sv.find("h55_survivor_m_dns") != std::string_view::npos)
    return "[斗牛士]";
  if (sv.find("girl") != std::string_view::npos)
    return "[小女孩]";
  if (sv.find("h55_survivor_w_moju") != std::string_view::npos)
    return "[默剧艺人]";
  return (char *)类名;
}
static const char *getboss(const char *类名) {
  std::string_view sv(类名);
  if (sv.find("joseph") != std::string_view::npos)
    return "[约瑟夫]";
  if (sv.find("banshee") != std::string_view::npos)
    return "[红蝶]";
  if (sv.find("h55_banshee.gim") != std::string_view::npos)
    return "[红蝶]";
  if (sv.find("sxwd") != std::string_view::npos)
    return "[小丑]";
  if (sv.find("chuanhuo") != std::string_view::npos)
    return "[厂长残火]";
  if (sv.find("redqueen") != std::string_view::npos)
    return "[红夫人]";
  if (sv.find("dm65_butcher_ll") != std::string_view::npos)
    return "[鹿头]";
  if (sv.find("ripper") != std::string_view::npos)
    return "[杰克]";
  if (sv.find("spider") != std::string_view::npos)
    return "[蜘蛛]";
  if (sv.find("lizard") != std::string_view::npos)
    return "[孽蜥]";
  if (sv.find("hastur") != std::string_view::npos)
    return "[黄衣之主]";
  if (sv.find("wuchang") != std::string_view::npos)
    return "[黑白无常]";
  if (sv.find("burke") != std::string_view::npos)
    return "[疯眼]";
  if (sv.find("yidhra_xintu") != std::string_view::npos)
    return "[原生信徒]";
  if (sv.find("yidhra") != std::string_view::npos)
    return "[梦之女巫]";
  if (sv.find("boy_shu") != std::string_view::npos)
    return "[安息松]";
  if (sv.find("boy") != std::string_view::npos)
    return "[爱哭鬼]";
  if (sv.find("bomber") != std::string_view::npos ||
      sv.find("bonbon") != std::string_view::npos)
    return "[26号守卫]";
  if (sv.find("messager") != std::string_view::npos)
    return "[噩梦]";
  if (sv.find("joan") != std::string_view::npos)
    return "[使徒]";
  if (sv.find("joan_cat") != std::string_view::npos)
    return "[使徒的猫]";
  if (sv.find("paganini") != std::string_view::npos)
    return "[小提琴家]";
  if (sv.find("sculptor") != std::string_view::npos)
    return "[雕刻家]";
  if (sv.find("polun") != std::string_view::npos)
    return "[破轮]";
  if (sv.find("frank") != std::string_view::npos)
    return "[博士]";
  if (sv.find("yunv") != std::string_view::npos)
    return "[渔女]";
  if (sv.find("laxiang") != std::string_view::npos ||
      sv.find("wax") != std::string_view::npos)
    return "[蜡像师]";
  if (sv.find("lady") != std::string_view::npos)
    return "[记录员]";
  if (sv.find("ithaqua") != std::string_view::npos)
    return "[守夜人]";
  if (sv.find("famingjia") != std::string_view::npos ||
      sv.find("hermit") != std::string_view::npos)
    return "[隐士]";
  if (sv.find("goat") != std::string_view::npos)
    return "[歌剧演员]";
  if (sv.find("goat_shadow.gim") != std::string_view::npos)
    return "[歌剧演员]";
  if (sv.find("h55_prop_balloon01") != std::string_view::npos)
    return "[歌剧演员]";
  if (sv.find("h55_prop_balloon01.gim") != std::string_view::npos)
    return "[歌剧演员]";
  if (sv.find("spkantan") != std::string_view::npos)
    return "[愚人金]";
  if (sv.find("yith") != std::string_view::npos)
    return "[时空之影]";
  if (sv.find("butcher") != std::string_view::npos)
    return "[厂长]";
  if (sv.find("billy") != std::string_view::npos)
    return "[台球手]";
  if (sv.find("spkunc") != std::string_view::npos)
    return "[女王蜂]";
  if (sv.find("space") != std::string_view::npos)
    return "[坡脚羊]";
  if (sv.find("spzaji") != std::string_view::npos)
    return "[喧嚣]";
  if (sv.find("grocer") != std::string_view::npos)
    return "[杂货商]";
  if (sv.find("dentist") != std::string_view::npos)
    return "[牙医]";
  return (char *)类名;
}
static const char *getprop(const char *类名) {
  std::string_view sv(类名);
  if (sv.find("h55_pendant_inject") != std::string_view::npos)
    return "[镇静剂]";
  if (sv.find("h55_pendant_moshubang") != std::string_view::npos)
    return "[魔术棒]";
  if (sv.find("h55_pendant_flaregun") != std::string_view::npos)
    return "[信号枪]";
  if (sv.find("h55_pendant_huzhou") != std::string_view::npos)
    return "[护肘]";
  if (sv.find("h55_pendant_map") != std::string_view::npos)
    return "[地图]";
  if (sv.find("h55_pendant_book") != std::string_view::npos)
    return "[书]";
  if (sv.find("h55_pendant_gjx") != std::string_view::npos)
    return "[工具箱]";
  if (sv.find("h55_pendant_glim") != std::string_view::npos)
    return "[手电筒]";
  if (sv.find("h55_pendant_xiangshuiping") != std::string_view::npos)
    return "[忘忧之香]";
  if (sv.find("h55_pendant_controller") != std::string_view::npos)
    return "[遥控器]";
  if (sv.find("h55_pendant_football") != std::string_view::npos)
    return "[橄榄球]";
  if (sv.find("h55_pendant_huaibiao") != std::string_view::npos)
    return "[怀表]";
  if (sv.find("h55_pendant_puppet") != std::string_view::npos)
    return "[厂长傀儡]";
  if (sv.find("h55_pendant_tower") != std::string_view::npos)
    return "[窥视者]";
  if (sv.find("h55_pendant_huojian") != std::string_view::npos)
    return "[哭丑的浮木]";
  if (sv.find("h55_pendant_banqiu") != std::string_view::npos)
    return "[板球]";
  if (sv.find("h55_pendant_pig") != std::string_view::npos)
    return "[野猪or虫群]";
  if (sv.find("h55_pendant_maildog") != std::string_view::npos)
    return "哈基汪";
  if (sv.find("h55_pendant_patro") != std::string_view::npos)
    return "[巡视者]";
  if (sv.find("bianzi") != std::string_view::npos)
    return "牛鞭";
  if (sv.find("h55_pendant_owl") != std::string_view::npos)
    return "鸟";
  if (sv.find("h55_pendant_wushu_xiao") != std::string_view::npos)
    return "棍";
  if (sv.find("h55_pendant_bow") != std::string_view::npos)
    return "弓";
  if (sv.find("h55_prop_tieqiao") != std::string_view::npos)
    return "铁锹";
  if (sv.find("h55_pendant_dxzh_toukui") != std::string_view::npos)
    return "头盔";
  if (sv.find("h55_pendant_qx") != std::string_view::npos)
    return "气象瓶";
  if (sv.find("h55_pendant_gouzhua_e") != std::string_view::npos)
    return "钩爪";
  return (char *)类名;
}
static const char *getscene(const char *类名) {
  std::string_view sv(类名);
  if (sv.find("h55_sleepingtown3_jpcat01low") != std::string_view::npos)
    return "哈基米";
  if (sv.find("h55_playground_lion") != std::string_view::npos)
    return "狮子";
  if (sv.find("dm65_scene_prop_76") != std::string_view::npos)
    return "地窖";
  if (sv.find("h55_pendant_horse.gim") != std::string_view::npos)
    return "小型马";
  if (sv.find("h55_pendant_horse2.gim") != std::string_view::npos)
    return "大型马";
  if (sv.find("h55_pet_liequan") != std::string_view::npos)
    return "猎犬";
  return (char *)类名;
}