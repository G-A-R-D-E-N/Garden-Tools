#pragma once

namespace fo4ide::plugin::scenefields {

struct PayloadField {
    const char* key;
    const char* path;
    bool link;
};

// Constraint: every path must exist in Radicle's scene writer payload table with the same link flag.
inline constexpr PayloadField k_payloadFields[] = {
    {"topic", "SCEN\\Action\\Topic", true},
    {"dialogueSubtype", "SCEN\\Action\\DialogueSubtype", true},
    {"animArchType", "SCEN\\Action\\AnimArchType", true},
    {"audioOutputOverride", "SCEN\\Action\\AudioOutputOverride", true},
    {"startSceneTopic", "SCEN\\Action\\StartSceneTopic", true},
    {"playSound", "SCEN\\Action\\PlaySound", true},
    {"playerPositiveResponse", "SCEN\\Action\\PlayerPositiveResponse", true},
    {"playerNegativeResponse", "SCEN\\Action\\PlayerNegativeResponse", true},
    {"playerNeutralResponse", "SCEN\\Action\\PlayerNeutralResponse", true},
    {"playerQuestionResponse", "SCEN\\Action\\PlayerQuestionResponse", true},
    {"npcPositiveResponse", "SCEN\\Action\\NpcPositiveResponse", true},
    {"npcNegativeResponse", "SCEN\\Action\\NpcNegativeResponse", true},
    {"npcNeutralResponse", "SCEN\\Action\\NpcNeutralResponse", true},
    {"npcQuestionResponse", "SCEN\\Action\\NpcQuestionResponse", true},
    {"loopingMax", "SCEN\\Action\\LoopingMax", false},
    {"loopingMin", "SCEN\\Action\\LoopingMin", false},
    {"cameraFovOnPlayerCamera", "SCEN\\Action\\CameraFovOnPlayerCamera", false},
    {"cameraRateOfChange", "SCEN\\Action\\CameraRateOfChange", false},
    {"emotionType", "SCEN\\Action\\EmotionType", false},
    {"emotionValue", "SCEN\\Action\\EmotionValue", false},
    {"timerMax", "SCEN\\Action\\TimerMax", false},
    {"timerMin", "SCEN\\Action\\TimerMin", false},
    {"dialogueTargetActor", "SCEN\\Action\\DialogueTargetActor", false},
};

}
