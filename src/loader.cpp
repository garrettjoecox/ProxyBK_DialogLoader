#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <sstream>
#include <stdexcept>

#include "helpers.hpp"
#include "mod_recomp.h"

namespace fs = std::filesystem;

fs::path MOD_FOLDER_PATH;

std::unordered_map<int32_t, std::array<uint8_t, 0x1000>> dialogMap;
std::unordered_map<int32_t, std::array<uint8_t, 0x1000>> quizQMap;
std::unordered_map<int32_t, std::array<uint8_t, 0x1000>> gruntyQMap;

struct BKString {
    uint8_t cmd;
    std::vector<uint8_t> string;
};

struct Dialog {
    std::vector<BKString> bottom;
    std::vector<BKString> top;
};

static Dialog LoadDialogFromPath(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + path);
    }

    Dialog result;
    std::string line;
    std::vector<BKString>* current_section = nullptr;

    // Poor man's YAML parsing since I don't want to add a dependency on a YAML library just for this
    while (std::getline(file, line)) {
        // Trim leading spaces
        size_t start = line.find_first_not_of(" \t\r");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        if (line.find("type:") == 0) {
            if (line.find("Dialog") == std::string::npos) {
                throw std::runtime_error("Expected Dialog type");
            }
        }
        else if (line.find("bottom:") == 0) {
            current_section = &result.bottom;
        }
        else if (line.find("top:") == 0) {
            current_section = &result.top;
        }
        else if (line.find("- {") != std::string::npos && current_section) {
            // Parse inline format: - { cmd: 0x83, string: "text" }
            BKString bkstr;

            // Extract cmd value
            size_t cmd_pos = line.find("cmd:");
            if (cmd_pos != std::string::npos) {
                size_t cmd_start = cmd_pos + 4;
                size_t cmd_end = line.find_first_of(",}", cmd_start);
                std::string cmd_str = line.substr(cmd_start, cmd_end - cmd_start);
                // Trim spaces
                cmd_str = cmd_str.substr(cmd_str.find_first_not_of(" \t"));
                cmd_str = cmd_str.substr(0, cmd_str.find_last_not_of(" \t") + 1);
                // Parse hex or decimal
                bkstr.cmd = static_cast<uint8_t>(std::stoi(cmd_str, nullptr, 0));
            }

            // Extract string value
            size_t string_pos = line.find("string:");
            if (string_pos != std::string::npos) {
                size_t string_start = string_pos + 7;
                // Find opening quote
                size_t quote_start = line.find_first_of("\"'", string_start);
                if (quote_start != std::string::npos) {
                    char quote_char = line[quote_start];
                    size_t quote_end = line.find(quote_char, quote_start + 1);
                    if (quote_end != std::string::npos) {
                        std::string text = line.substr(quote_start + 1, quote_end - quote_start - 1);
                        bkstr.string = std::vector<uint8_t>(text.begin(), text.end());
                    }
                } else {
                    // Empty string case
                    bkstr.string.clear();
                }
            }

            current_section->push_back(bkstr);
        }
    }

    return result;
}

static std::vector<uint8_t> ConvertUTF8ToLatin1(const std::vector<uint8_t>& input) {
    std::vector<uint8_t> result;
    
    for (size_t i = 0; i < input.size(); ++i) {
        uint8_t byte = input[i];
        
        // ASCII range (0x00-0x7F): pass through as-is
        if (byte < 0x80) {
            result.push_back(byte);
        }
        // UTF-8 2-byte sequence for Latin-1 supplement (0xC2-0xC3)
        else if ((byte == 0xC2 || byte == 0xC3) && i + 1 < input.size()) {
            uint8_t next_byte = input[i + 1];
            
            // Validate that it's a valid UTF-8 continuation byte (0x80-0xBF)
            if ((next_byte & 0xC0) == 0x80) {
                // Decode UTF-8 to Unicode code point
                uint32_t codepoint = ((byte & 0x1F) << 6) | (next_byte & 0x3F);
                
                // Code points 0x80-0xFF map directly to ISO-8859-1
                if (codepoint >= 0x80 && codepoint <= 0xFF) {
                    result.push_back(static_cast<uint8_t>(codepoint));
                    i++; // Skip the continuation byte
                } else {
                    // Shouldn't happen with 0xC2-0xC3, but handle gracefully
                    result.push_back('?'); // Replacement character
                    i++;
                }
            } else {
                // Invalid UTF-8 sequence
                result.push_back('?');
            }
        }
        // UTF-8 3+ byte sequences: not representable in ISO-8859-1
        else if ((byte & 0xE0) == 0xC0) {
            // 2-byte sequence but not Latin-1 range
            result.push_back('?');
            if (i + 1 < input.size() && (input[i + 1] & 0xC0) == 0x80) {
                i++; // Skip continuation byte
            }
        }
        else if ((byte & 0xF0) == 0xE0) {
            // 3-byte sequence
            result.push_back('?');
            if (i + 1 < input.size() && (input[i + 1] & 0xC0) == 0x80) i++;
            if (i + 1 < input.size() && (input[i + 1] & 0xC0) == 0x80) i++;
        }
        else if ((byte & 0xF8) == 0xF0) {
            // 4-byte sequence
            result.push_back('?');
            if (i + 1 < input.size() && (input[i + 1] & 0xC0) == 0x80) i++;
            if (i + 1 < input.size() && (input[i + 1] & 0xC0) == 0x80) i++;
            if (i + 1 < input.size() && (input[i + 1] & 0xC0) == 0x80) i++;
        }
        else {
            // Invalid UTF-8 or continuation byte in wrong position
            result.push_back('?');
        }
    }
    
    return result;
}

std::vector<uint8_t> ConvertDialogToBytes(const Dialog& dialog) {
    std::vector<uint8_t> out = {0x01, 0x03, 0x00};
    
    // Bottom texts
    out.push_back(static_cast<uint8_t>(dialog.bottom.size()));
    for (const auto& text : dialog.bottom) {
        std::vector<uint8_t> converted = ConvertUTF8ToLatin1(text.string);
        out.push_back(text.cmd);
        out.push_back(static_cast<uint8_t>(converted.size() + 1)); // +1 for null terminator
        out.insert(out.end(), converted.begin(), converted.end());
        out.push_back(0x00);
    }
    
    // Top texts
    out.push_back(static_cast<uint8_t>(dialog.top.size()));
    for (const auto& text : dialog.top) {
        std::vector<uint8_t> converted = ConvertUTF8ToLatin1(text.string);
        out.push_back(text.cmd);
        out.push_back(static_cast<uint8_t>(converted.size() + 1)); // +1 for null terminator
        out.insert(out.end(), converted.begin(), converted.end());
        out.push_back(0x00);
    }
    
    // Pad to 4-byte alignment for endianness swap
    while (out.size() % 4 != 0) {
        out.push_back(0);
    }
    
    // Swap endianness (4-byte chunks)
    for (size_t i = 0; i < out.size(); i += 4) {
        std::swap(out[i + 0], out[i + 3]);
        std::swap(out[i + 1], out[i + 2]);
    }
    
    return out;
}

void RefreshDialog(int32_t textId) {
    dialogMap.erase(textId);

    // Convert textId to hex string for filename (padded to 4 chars)
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << textId;
    std::string fileName = ss.str() + ".dialog";
    
    fs::path dialogPath = MOD_FOLDER_PATH / "DialogLoader" / "dialog";
    if (!fs::exists(dialogPath)) return;
    
    // Recursively search for the file
    fs::path filePath;
    bool found = false;
    for (const auto& entry : fs::recursive_directory_iterator(dialogPath)) {
        if (entry.is_regular_file() && entry.path().filename() == fileName) {
            filePath = entry.path();
            found = true;
            break;
        }
    }
    
    if (!found) return;

    try {
        Dialog dialog = LoadDialogFromPath(filePath.string());
        std::vector<uint8_t> binary = ConvertDialogToBytes(dialog);

        dialogMap[textId] = {};
        size_t copySize = std::min(binary.size(), size_t(0x1000));
        std::copy_n(binary.begin(), copySize, dialogMap[textId].begin());
    } catch (const std::exception& e) {
        printf("[ProxyBK_DialogLoader] Error loading %s: %s\n", filePath.string().c_str(), e.what());
    }
}

extern "C" {

DLLEXPORT uint32_t recomp_api_version = 1;

DLLEXPORT void DialogLoader_RefreshAll(uint8_t* rdram, recomp_context* ctx) {
    dialogMap.clear();

    fs::path mainPath = MOD_FOLDER_PATH / "DialogLoader";
    if (!fs::exists(mainPath)) {
        fs::create_directories(mainPath);
    }

    fs::path dialogPath = mainPath / "dialog";
    if (!fs::exists(dialogPath)) {
        fs::create_directories(dialogPath);
    }

    for (const auto& entry : fs::recursive_directory_iterator(dialogPath)) {
        if (!entry.is_regular_file()) continue;
        fs::path filePath = entry.path();
        if (filePath.extension() != ".dialog") continue;

        std::string fileName = filePath.stem().string();
        int32_t textId = std::stoi(fileName, nullptr, 16);

        RefreshDialog(textId);
    }

    _return(ctx, 0);
}

DLLEXPORT void DialogLoader_SetModsFolderPath(uint8_t* rdram, recomp_context* ctx) {
    MOD_FOLDER_PATH = fs::path(_arg_string<0>(rdram, ctx));

    printf("[ProxyBK_DialogLoader] Mods folder path set to %s\n", MOD_FOLDER_PATH.string().c_str());

    _return(ctx, 0);
}

DLLEXPORT void DialogLoader_RefreshDialog(uint8_t* rdram, recomp_context* ctx) {
    int32_t textId = _arg<0, int32_t>(rdram, ctx);

    RefreshDialog(textId);

    _return(ctx, 0);
}

DLLEXPORT void DialogLoader_RefreshQuizQ(uint8_t* rdram, recomp_context* ctx) {
    int32_t quizQId = _arg<0, int32_t>(rdram, ctx);

    // RefreshQuizQ(quizQId);

    _return(ctx, 0);
}

DLLEXPORT void DialogLoader_GetDialog(uint8_t* rdram, recomp_context* ctx) {
    int32_t textId = _arg<0, int32_t>(rdram, ctx);
    void* dest = _arg<1, void*>(rdram, ctx);

    if (dialogMap.count(textId) > 0) {
        uint8_t* dest_bytes = (uint8_t*)dest;
        const auto& src = dialogMap[textId];
        std::copy(src.begin(), src.end(), dest_bytes);
        _return(ctx, 1);
    } else {
        _return(ctx, 0);
    }
}

DLLEXPORT void DialogLoader_GetQuizQ(uint8_t* rdram, recomp_context* ctx) {
    int32_t quizQId = _arg<0, int32_t>(rdram, ctx);
    void* dest = _arg<1, void*>(rdram, ctx);

    _return(ctx, 0);
}

}
