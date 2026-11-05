#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/app/WikiService.hpp>

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() / ("fo4ide_wiki_test_" + id);
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void touch(const std::filesystem::path& path, std::string_view contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << contents;
}

// The C# test fixtures, kept verbatim so the extraction assertions match.
std::string functionPageHtml() {
    return R"(<!DOCTYPE html>
<html lang="en">
<head>
<title>GetBaseObject - ActiveMagicEffect</title>
<body>
<h1 id="firstHeading" class="firstHeading" lang="en">GetBaseObject - ActiveMagicEffect</h1>
<div id="mw-content-text" lang="en" dir="ltr" class="mw-content-ltr"><p><b>Member of:</b> <a href="ActiveMagicEffect_Script.html" title="ActiveMagicEffect Script">ActiveMagicEffect Script</a>
</p><p>Obtains the <a href="MagicEffect_Script.html" title="MagicEffect Script">MagicEffect</a> this active magic effect is based on.
</p>
<h2><span class="mw-headline" id="Syntax">Syntax</span></h2>
<div class="mw-geshi"><pre class="de1"><span class="kw3">MagicEffect</span> <span class="kw1">Function</span> <span class="kw5">GetBaseObject</span><span class="br0">(</span><span class="br0">)</span></pre></div>
<h2><span class="mw-headline" id="Parameters">Parameters</span></h2>
<p>None
</p>
<h2><span class="mw-headline" id="Return_Value">Return Value</span></h2>
<p>The <a href="MagicEffect_Script.html" title="MagicEffect Script">MagicEffect</a> this active effect is based on.
</p>
<h2><span class="mw-headline" id="Examples">Examples</span></h2>
<div class="mw-geshi"><pre class="de1">example code here</pre></div>
<h2><span class="mw-headline" id="Caveat">Caveat</span></h2>
<p>It is possible the effect ceases to exist before this call returns.
</p>
<h2><span class="mw-headline" id="See_Also">See Also</span></h2>
<div id="catlinks" class="catlinks">cats</div>
</div>
</body>
)";
}

std::string scriptPageHtml() {
    return R"(<!DOCTYPE html>
<html lang="en">
<head>
<title>ActiveMagicEffect Script</title>
<body>
<h1 id="firstHeading" class="firstHeading" lang="en">ActiveMagicEffect Script</h1>
<div id="mw-content-text" lang="en" dir="ltr" class="mw-content-ltr"><p><b>Extends:</b> <a href="ScriptObject_Script.html" title="ScriptObject Script">ScriptObject</a>
<br>
<b>Editor:</b> <a href="Magic_Effect.html" title="Magic Effect">Magic Effect</a>
</p><p>Script for the manipulation of active magic effects.
</p>
<h2><span class="mw-headline" id="Definition">Definition</span></h2>
<div class="mw-geshi"><pre class="de1"><span class="kw1">ScriptName</span> ActiveMagicEffect extends ScriptObject Native Hidden</pre></div>
<h2><span class="mw-headline" id="Properties">Properties</span></h2>
<p>None
</p>
<h2><span class="mw-headline" id="Global_Functions">Global_Functions</span></h2>
<p>None
</p>
<h2><span class="mw-headline" id="Member_Functions">Member Functions</span></h2>
<ul><li>Function <a href="Dispel_-_ActiveMagicEffect.html" title="Dispel - ActiveMagicEffect">Dispel</a>()
<ul><li>Dispels this active magic effect.</li></ul></li>
<li>MagicEffect Function <a href="GetBaseObject_-_ActiveMagicEffect.html" title="GetBaseObject - ActiveMagicEffect">GetBaseObject</a>()
<ul><li>Obtains the MagicEffect this active magic effect is based on.</li></ul></li></ul>
<h2><span class="mw-headline" id="Events">Events</span></h2>
<ul><li>Event <a href="OnEffectStart_-_ActiveMagicEffect.html" title="OnEffectStart - ActiveMagicEffect">OnEffectStart</a>(Actor akTarget, Actor akCaster)
<ul><li>Event received when this effect starts.</li></ul></li></ul>
<h2><span class="mw-headline" id="Notes">Notes</span></h2>
<ul><li>Notes here.</li></ul>
<h2><span class="mw-headline" id="See_Also">See Also</span></h2>
<div id="catlinks" class="catlinks">cats</div>
</div>
</body>
)";
}

std::filesystem::path makeWikiRoot() {
    const auto root = tempRoot();
    const auto wiki = root / "fallout4";
    touch(wiki / "GetBaseObject_-_ActiveMagicEffect.html", functionPageHtml());
    touch(wiki / "ActiveMagicEffect_Script.html", scriptPageHtml());
    return wiki;
}

}  // namespace

TEST_CASE("LookupFunction extracts syntax, parameters and return value") {
    const auto root = makeWikiRoot();
    Cleanup cleanup{root.parent_path()};

    const auto result = fo4ide::app::wiki::lookupFunction(root.string(), "ActiveMagicEffect", "GetBaseObject");
    REQUIRE(result.find("Member of: ActiveMagicEffect Script") != std::string::npos);
    CHECK(result.find("Syntax:") != std::string::npos);
    CHECK(result.find("MagicEffect Function GetBaseObject ( )") != std::string::npos);
    CHECK(result.find("Parameters: None") != std::string::npos);
    CHECK(result.find("Return Value:") != std::string::npos);
    CHECK(result.find("this active effect is based on") != std::string::npos);
    CHECK(result.find("Caveat:") != std::string::npos);
    CHECK(result.find("ceases to exist") != std::string::npos);
}

TEST_CASE("LookupFunction without script finds a unique match") {
    const auto root = makeWikiRoot();
    Cleanup cleanup{root.parent_path()};

    const auto result = fo4ide::app::wiki::lookupFunction(root.string(), "", "GetBaseObject");
    CHECK(result.find("Syntax:") != std::string::npos);
}

TEST_CASE("LookupFunction with an unknown function returns helpful guidance") {
    const auto root = makeWikiRoot();
    Cleanup cleanup{root.parent_path()};

    const auto result = fo4ide::app::wiki::lookupFunction(root.string(), "", "TotallyMadeUpFunction");
    CHECK(result.find("No CK wiki page found") != std::string::npos);
}

TEST_CASE("LookupFunction with an ambiguous name lists the owners") {
    const auto root = makeWikiRoot();
    Cleanup cleanup{root.parent_path()};
    touch(root / "GetBaseObject_-_AnotherScript.html",
          "<html><h1>GetBaseObject - AnotherScript</h1></html>");

    const auto result = fo4ide::app::wiki::lookupFunction(root.string(), "", "GetBaseObject");
    CHECK(result.find("defined on multiple scripts") != std::string::npos);
    CHECK(result.find("ActiveMagicEffect") != std::string::npos);
    CHECK(result.find("AnotherScript") != std::string::npos);
    CHECK(result.find("Pass 'script' to disambiguate") != std::string::npos);
}

TEST_CASE("LookupFunction without a configured wiki returns the config message") {
    const auto result = fo4ide::app::wiki::lookupFunction("", "", "GetBaseObject");
    CHECK(result.find("No CK wiki mirror configured") != std::string::npos);
    CHECK(result.find("--ck-wiki") != std::string::npos);
}

TEST_CASE("LookupScriptInfo extracts extends, member functions and events") {
    const auto root = makeWikiRoot();
    Cleanup cleanup{root.parent_path()};

    const auto result = fo4ide::app::wiki::lookupScriptInfo(root.string(), "ActiveMagicEffect");
    CHECK(result.find("Extends: ScriptObject") != std::string::npos);
    CHECK(result.find("Member Functions:") != std::string::npos);
    CHECK(result.find("GetBaseObject") != std::string::npos);
    CHECK(result.find("Dispel") != std::string::npos);
    CHECK(result.find("Events:") != std::string::npos);
    CHECK(result.find("OnEffectStart") != std::string::npos);
    // The page's Global Functions section is 'None' and should be omitted.
    CHECK(result.find("Global Functions:") == std::string::npos);
}

TEST_CASE("LookupScriptInfo accepts a trailing Script suffix") {
    const auto root = makeWikiRoot();
    Cleanup cleanup{root.parent_path()};

    const auto result = fo4ide::app::wiki::lookupScriptInfo(root.string(), "ActiveMagicEffect_Script");
    CHECK(result.find("Extends: ScriptObject") != std::string::npos);
}
