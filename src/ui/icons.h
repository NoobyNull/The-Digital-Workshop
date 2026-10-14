#pragma once

// Icon definitions using FontAwesome 6 Free Solid glyphs
// These are UTF-8 encoded Unicode codepoints merged into the default ImGui font

namespace dw {
namespace icons {

// File operations
constexpr const char* ICON_NEW = "\xef\x85\x9b";    // f15b file
constexpr const char* ICON_OPEN = "\xef\x81\xbc";   // f07c folder-open
constexpr const char* ICON_SAVE = "\xef\x83\x87";   // f0c7 floppy-disk
constexpr const char* ICON_IMPORT = "\xef\x95\x9e"; // f56e file-import
constexpr const char* ICON_EXPORT = "\xef\x95\x9c"; // f56c file-export

// View controls
constexpr const char* ICON_ZOOM_IN = "\xef\x80\x8e";  // f00e magnifying-glass-plus
constexpr const char* ICON_ZOOM_OUT = "\xef\x80\x90"; // f010 magnifying-glass-minus
constexpr const char* ICON_FIT = "\xef\x81\xa5";      // f065 expand
constexpr const char* ICON_GRID = "\xef\x80\x8a";     // f00a table-cells

// Actions
constexpr const char* ICON_ADD = "\xef\x81\xa7";     // f067 plus
constexpr const char* ICON_REMOVE = "\xef\x81\xa8";  // f068 minus
constexpr const char* ICON_DELETE = "\xef\x87\xb8";  // f1f8 trash
constexpr const char* ICON_SEARCH = "\xef\x80\x82";  // f002 magnifying-glass
constexpr const char* ICON_FILTER = "\xef\x82\xb0";  // f0b0 filter
constexpr const char* ICON_REFRESH = "\xef\x80\xa1"; // f021 arrows-rotate

// Object types
constexpr const char* ICON_MODEL = "\xef\x86\xb2";   // f1b2 cube
constexpr const char* ICON_PROJECT = "\xef\x81\xac"; // f06c folder
constexpr const char* ICON_FOLDER = "\xef\x81\xac";  // f06c folder
constexpr const char* ICON_FILE = "\xef\x85\x9b";    // f15b file

// Settings and status
constexpr const char* ICON_SETTINGS = "\xef\x80\x93"; // f013 gear
constexpr const char* ICON_INFO = "\xef\x81\x9a";     // f05a circle-info
constexpr const char* ICON_WARNING = "\xef\x81\xb1";  // f071 triangle-exclamation
constexpr const char* ICON_ERROR = "\xef\x81\x97";    // f057 circle-xmark
constexpr const char* ICON_QUESTION = "\xef\x81\x99"; // f059 circle-question

// Status marks
constexpr const char* ICON_CHECK = "\xef\x80\x8c"; // f00c check

// Toolbox / favorites
constexpr const char* ICON_STAR = "\xef\x80\x85"; // f005 star

// View modes
constexpr const char* ICON_LIST = "\xef\x80\x8b"; // f00b list

// Domain-specific
constexpr const char* ICON_GCODE = "\xef\x87\x81";      // f1c1 file-code (G-code files)
constexpr const char* ICON_MATERIAL = "\xef\x87\xbc";   // f1fc paintbrush (materials)
constexpr const char* ICON_NOTES = "\xef\x89\x89";      // f249 sticky-note (notes)

// Materials-specific
constexpr const char* ICON_ASSIGN = "\xef\x87\x87";     // f1c7 link (chain)
constexpr const char* ICON_WAND = "\xef\x83\x90";       // f0d0 wand-magic-sparkles
constexpr const char* ICON_PAINTBRUSH = "\xef\x87\xbc"; // f1fc paintbrush

// Jog / navigation arrows
constexpr const char* ICON_ARROW_UP = "\xef\x81\xa2";    // f062 arrow-up
constexpr const char* ICON_ARROW_DOWN = "\xef\x81\xa3";  // f063 arrow-down
constexpr const char* ICON_ARROW_LEFT = "\xef\x81\xa0";  // f060 arrow-left
constexpr const char* ICON_ARROW_RIGHT = "\xef\x81\xa1"; // f061 arrow-right
constexpr const char* ICON_HOME = "\xef\x80\x95";        // f015 house
constexpr const char* ICON_LOCK = "\xef\x80\xa3";         // f023 lock
constexpr const char* ICON_LOCK_OPEN = "\xef\x82\x9c";   // f09c lock-open

// Playback / sender
constexpr const char* ICON_PLAY = "\xef\x81\x8b";       // f04b play
constexpr const char* ICON_PAUSE = "\xef\x81\x8c";      // f04c pause
constexpr const char* ICON_STOP = "\xef\x81\x8d";       // f04d stop
constexpr const char* ICON_PLUG = "\xef\x87\xa6";       // f1e6 plug
constexpr const char* ICON_TERMINAL = "\xef\x84\xa0";   // f120 terminal
constexpr const char* ICON_UNLINK = "\xef\x84\xa7";     // f127 link-slash

} // namespace icons

// Convenience struct for simpler access: Icons::Model instead of icons::ICON_MODEL
struct Icons {
    static constexpr const char* New = icons::ICON_NEW;
    static constexpr const char* Open = icons::ICON_OPEN;
    static constexpr const char* Save = icons::ICON_SAVE;
    static constexpr const char* Import = icons::ICON_IMPORT;
    static constexpr const char* Export = icons::ICON_EXPORT;
    static constexpr const char* ZoomIn = icons::ICON_ZOOM_IN;
    static constexpr const char* ZoomOut = icons::ICON_ZOOM_OUT;
    static constexpr const char* Fit = icons::ICON_FIT;
    static constexpr const char* Grid = icons::ICON_GRID;
    static constexpr const char* Add = icons::ICON_ADD;
    static constexpr const char* Remove = icons::ICON_REMOVE;
    static constexpr const char* Delete = icons::ICON_DELETE;
    static constexpr const char* Search = icons::ICON_SEARCH;
    static constexpr const char* Filter = icons::ICON_FILTER;
    static constexpr const char* Refresh = icons::ICON_REFRESH;
    static constexpr const char* Model = icons::ICON_MODEL;
    static constexpr const char* Project = icons::ICON_PROJECT;
    static constexpr const char* Folder = icons::ICON_FOLDER;
    static constexpr const char* File = icons::ICON_FILE;
    static constexpr const char* Settings = icons::ICON_SETTINGS;
    static constexpr const char* Info = icons::ICON_INFO;
    static constexpr const char* Warning = icons::ICON_WARNING;
    static constexpr const char* Error = icons::ICON_ERROR;
    static constexpr const char* Question = icons::ICON_QUESTION;
    static constexpr const char* Check = icons::ICON_CHECK;
    static constexpr const char* Star = icons::ICON_STAR;
    static constexpr const char* List = icons::ICON_LIST;
    static constexpr const char* GCode = icons::ICON_GCODE;
    static constexpr const char* Material = icons::ICON_MATERIAL;
    static constexpr const char* Notes = icons::ICON_NOTES;
    static constexpr const char* Assign = icons::ICON_ASSIGN;
    static constexpr const char* Wand = icons::ICON_WAND;
    static constexpr const char* Paintbrush = icons::ICON_PAINTBRUSH;
    static constexpr const char* ArrowUp = icons::ICON_ARROW_UP;
    static constexpr const char* ArrowDown = icons::ICON_ARROW_DOWN;
    static constexpr const char* ArrowLeft = icons::ICON_ARROW_LEFT;
    static constexpr const char* ArrowRight = icons::ICON_ARROW_RIGHT;
    static constexpr const char* Home = icons::ICON_HOME;
    static constexpr const char* Lock = icons::ICON_LOCK;
    static constexpr const char* LockOpen = icons::ICON_LOCK_OPEN;
    static constexpr const char* Play = icons::ICON_PLAY;
    static constexpr const char* Pause = icons::ICON_PAUSE;
    static constexpr const char* Stop = icons::ICON_STOP;
    static constexpr const char* Plug = icons::ICON_PLUG;
    static constexpr const char* Terminal = icons::ICON_TERMINAL;
    static constexpr const char* Unlink = icons::ICON_UNLINK;
};

} // namespace dw
