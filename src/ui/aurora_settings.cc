// Copyright 2026 NamKrub
// SPDX-License-Identifier: Apache-2.0

#include <adwaita.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <vector>
#include <unistd.h>

#include "runtime/external_client_detector.h"

namespace {

struct State {
  struct ClientRow {
    const char* id = nullptr;
    AdwActionRow* row = nullptr;
    GtkLabel* state = nullptr;
  };

  GtkDropDown* mode = nullptr;
  AdwSwitchRow* controller = nullptr;
  AdwSwitchRow* console = nullptr;
  AdwSwitchRow* share_activity = nullptr;
  AdwSwitchRow* discord_join = nullptr;
  AdwSwitchRow* download = nullptr;
  AdwSwitchRow* hidpi = nullptr;
  AdwSwitchRow* touch = nullptr;
  AdwSwitchRow* mobile_experience = nullptr;
  AdwSwitchRow* mobile_home = nullptr;
  AdwSwitchRow* legacy = nullptr;
  AdwSwitchRow* server_location = nullptr;
  AdwSwitchRow* close_on_leave = nullptr;
  AdwSwitchRow* discord_place = nullptr;
  AdwSwitchRow* discord_elapsed = nullptr;
  AdwSwitchRow* controller_extra = nullptr;
  GtkButton* controller_arrow = nullptr;
  GtkButton* discord_arrow = nullptr;
  GtkButton* touch_arrow = nullptr;
  AdwSwitchRow* vsync = nullptr;
  AdwSwitchRow* threaded = nullptr;
  AdwSwitchRow* gamemode = nullptr;
  AdwSwitchRow* discord = nullptr;
  AdwSwitchRow* updates = nullptr;
  AdwSwitchRow* opengl = nullptr;
  GtkLabel* status = nullptr;
  GMainLoop* loop = nullptr;
  guint client_poll_source = 0;
  std::vector<ClientRow> client_rows;
  aurora::runtime::ExternalClientDetector client_detector;
  std::filesystem::path repo;
  std::filesystem::path config;
};

const char* ModeName(guint selected) {
  static constexpr const char* kModes[] = {
      "balanced", "performance", "battery", "angle-compat", "opengl-rescue"};
  return selected < 5 ? kModes[selected] : kModes[0];
}

std::filesystem::path ExecutablePath() {
  char buffer[4096] = {};
  const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (length <= 0) return {};
  buffer[length] = '\0';
  return std::filesystem::path(buffer);
}

std::filesystem::path ConfigPath() {
  const char* configured = std::getenv("XDG_CONFIG_HOME");
  const char* home = std::getenv("HOME");
  const std::filesystem::path root =
      configured != nullptr && configured[0] == '/' ? configured :
      std::filesystem::path(home != nullptr ? home : ".") / ".config";
  return root / "aurora/config.yaml";
}

std::string ReplaceSetting(std::string contents, const std::string& section,
                           const std::string& key, const std::string& value) {
  const std::string section_marker = section + ":";
  const std::string key_marker = key + ":";
  std::size_t section_pos = contents.find(section_marker);
  if (section_pos == std::string::npos) return contents;
  const std::size_t next_section = contents.find("\n", section_pos);
  std::size_t pos = next_section == std::string::npos ? contents.size() : next_section + 1;
  while (pos < contents.size()) {
    const std::size_t line_end = contents.find('\n', pos);
    const std::size_t end = line_end == std::string::npos ? contents.size() : line_end;
    if (end > pos && contents[pos] != ' ' && contents[pos] != '\t') break;
    if (contents.compare(pos, key_marker.size(), key_marker) == 0 ||
        (contents.compare(pos, 2, "  ") == 0 &&
         contents.compare(pos + 2, key_marker.size(), key_marker) == 0) ||
        (contents.compare(pos, 4, "    ") == 0 &&
         contents.compare(pos + 4, key_marker.size(), key_marker) == 0)) {
      const std::size_t colon = contents.find(':', pos);
      const std::size_t value_end = contents.find_first_not_of(" \t", colon + 1);
      contents.replace(value_end, end - value_end, value);
      return contents;
    }
    if (line_end == std::string::npos) break;
    pos = line_end + 1;
  }
  return contents;
}

bool CopyProfile(State* state, std::string* error) {
  const std::filesystem::path source =
      state->repo / "config/profiles" / ModeName(0) /
      "config.yaml";
  std::ifstream input(source);
  if (!input) { *error = "เปิดไฟล์โหมดไม่สำเร็จ"; return false; }
  std::string contents((std::istreambuf_iterator<char>(input)), {});
  contents = ReplaceSetting(contents, "graphics", "vsync",
                            adw_switch_row_get_active(state->vsync) ? "on" : "off");
  contents = ReplaceSetting(contents, "performance", "multithreaded_rendering",
                            adw_switch_row_get_active(state->threaded) ? "true" : "false");
  contents = ReplaceSetting(contents, "performance", "gamemode",
                            adw_switch_row_get_active(state->gamemode) ? "on" : "off");
  contents = ReplaceSetting(contents, "discord_rpc", "enabled",
                            adw_switch_row_get_active(state->share_activity) ? "true" : "false");
  contents = ReplaceSetting(contents, "discord_rpc", "show_place_name",
                            adw_switch_row_get_active(state->discord_place) ? "true" : "false");
  contents = ReplaceSetting(contents, "discord_rpc", "show_elapsed_time",
                            adw_switch_row_get_active(state->discord_elapsed) ? "true" : "false");
  contents = ReplaceSetting(contents, "join", "enabled",
                            adw_switch_row_get_active(state->discord_join) ? "true" : "false");
  contents = ReplaceSetting(contents, "updates", "automatic",
                            adw_switch_row_get_active(state->download) ? "true" : "false");
  contents = ReplaceSetting(contents, "graphics", "backend",
                            adw_switch_row_get_active(state->legacy) ? "opengl" : "direct-vulkan");
  contents = ReplaceSetting(contents, "input", "touch_enabled",
                            adw_switch_row_get_active(state->touch) ? "true" : "false");
  std::error_code filesystem_error;
  std::filesystem::create_directories(state->config.parent_path(), filesystem_error);
  if (filesystem_error) { *error = "สร้างโฟลเดอร์ตั้งค่าไม่สำเร็จ"; return false; }
  std::ofstream output(state->config, std::ios::trunc);
  if (!output) { *error = "บันทึกไฟล์ตั้งค่าไม่สำเร็จ"; return false; }
  output << contents;
  if (!output) { *error = "เขียนไฟล์ตั้งค่าไม่สำเร็จ"; return false; }
  return true;
}

void Save(GtkButton*, gpointer data) {
  auto* state = static_cast<State*>(data);
  std::string error;
  if (CopyProfile(state, &error)) {
    gtk_label_set_text(state->status, "บันทึกแล้ว เปิด Aurora ใหม่เพื่อใช้โหมดนี้");
  } else {
    gtk_label_set_text(state->status, error.c_str());
  }
}

void Play(GtkButton*, gpointer data) {
  auto* state = static_cast<State*>(data);
  std::string error;
  if (!CopyProfile(state, &error)) {
    gtk_label_set_text(state->status, error.c_str());
    return;
  }
  const std::string runner = (state->repo / "build/aurora").string();
  const char* argv[] = {runner.c_str(), nullptr};
  GError* spawn_error = nullptr;
  if (!g_spawn_async(state->repo.c_str(), const_cast<char**>(argv), nullptr,
                     G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, &spawn_error)) {
    gtk_label_set_text(state->status, spawn_error->message);
    g_error_free(spawn_error);
    return;
  }
  g_main_loop_quit(state->loop);
}

void Close(GtkButton*, gpointer data) {
  g_main_loop_quit(static_cast<State*>(data)->loop);
}

void OnControllerToggled(GObject*, GParamSpec*, gpointer data) {
  auto* state = static_cast<State*>(data);
  const bool active = adw_switch_row_get_active(state->controller);
  if (!active) gtk_widget_set_visible(GTK_WIDGET(state->controller_extra), FALSE);
  gtk_image_set_from_icon_name(GTK_IMAGE(gtk_button_get_child(state->controller_arrow)),
                               "pan-down-symbolic");
}

void OnDiscordToggled(GObject*, GParamSpec*, gpointer data) {
  auto* state = static_cast<State*>(data);
  const bool active = adw_switch_row_get_active(state->share_activity);
  if (!active) {
    gtk_widget_set_visible(GTK_WIDGET(state->discord_place), FALSE);
    gtk_widget_set_visible(GTK_WIDGET(state->discord_elapsed), FALSE);
  }
  gtk_image_set_from_icon_name(GTK_IMAGE(gtk_button_get_child(state->discord_arrow)),
                               "pan-down-symbolic");
}

void OnTouchToggled(GObject*, GParamSpec*, gpointer data) {
  auto* state = static_cast<State*>(data);
  const bool active = adw_switch_row_get_active(state->touch);
  if (!active) {
    gtk_widget_set_visible(GTK_WIDGET(state->mobile_experience), FALSE);
    gtk_widget_set_visible(GTK_WIDGET(state->mobile_home), FALSE);
  }
  gtk_image_set_from_icon_name(GTK_IMAGE(gtk_button_get_child(state->touch_arrow)),
                               "pan-down-symbolic");
}

GtkButton* AddDropdownArrow(AdwSwitchRow* row, bool expanded) {
  GtkButton* arrow = GTK_BUTTON(gtk_button_new());
  gtk_button_set_child(arrow, gtk_image_new_from_icon_name(
      expanded ? "pan-up-symbolic" : "pan-down-symbolic"));
  gtk_button_set_has_frame(arrow, FALSE);
  gtk_widget_set_margin_start(GTK_WIDGET(arrow), 4);
  adw_action_row_add_suffix(ADW_ACTION_ROW(row), GTK_WIDGET(arrow));
  return arrow;
}

void ToggleChildRows(AdwSwitchRow* master, GtkButton* arrow,
                     std::initializer_list<GtkWidget*> children) {
  if (!adw_switch_row_get_active(master)) return;
  const bool expanded = !gtk_widget_get_visible(*children.begin());
  for (GtkWidget* child : children) gtk_widget_set_visible(child, expanded);
  gtk_image_set_from_icon_name(GTK_IMAGE(gtk_button_get_child(arrow)),
                               expanded ? "pan-up-symbolic" : "pan-down-symbolic");
}

void OnControllerArrowClicked(GtkButton*, gpointer data) {
  auto* state = static_cast<State*>(data);
  ToggleChildRows(state->controller, state->controller_arrow,
                  {GTK_WIDGET(state->controller_extra)});
}

void OnDiscordArrowClicked(GtkButton*, gpointer data) {
  auto* state = static_cast<State*>(data);
  ToggleChildRows(state->share_activity, state->discord_arrow,
                  {GTK_WIDGET(state->discord_place), GTK_WIDGET(state->discord_elapsed)});
}

void OnTouchArrowClicked(GtkButton*, gpointer data) {
  auto* state = static_cast<State*>(data);
  ToggleChildRows(state->touch, state->touch_arrow,
                  {GTK_WIDGET(state->mobile_experience), GTK_WIDGET(state->mobile_home)});
}

GtkWidget* SectionLabel(const char* text) {
  GtkWidget* label = gtk_label_new(text);
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_add_css_class(label, "heading");
  return label;
}

const char* ClientStateText(
    const aurora::runtime::ExternalClientStatus& status) {
  if (status.connected) return "เชื่อมต่อ";
  if (status.running) return "เปิด";
  return "ปิด";
}

void AddClientRow(State* state, GtkWidget* group, const char* id,
                  const char* title) {
  auto* row = ADW_ACTION_ROW(adw_action_row_new());
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
  adw_action_row_set_subtitle(ADW_ACTION_ROW(row),
                              "ตรวจจับสถานะในเครื่องอัตโนมัติ");
  auto* state_label = GTK_LABEL(gtk_label_new("กำลังตรวจสอบ..."));
  gtk_widget_add_css_class(GTK_WIDGET(state_label), "dim-label");
  adw_action_row_add_suffix(ADW_ACTION_ROW(row), GTK_WIDGET(state_label));
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), GTK_WIDGET(row));
  state->client_rows.push_back({id, row, state_label});
}

gboolean RefreshClients(gpointer data) {
  auto* state = static_cast<State*>(data);
  const std::vector<aurora::runtime::ExternalClientStatus> statuses =
      state->client_detector.Detect();
  for (const State::ClientRow& client_row : state->client_rows) {
    const auto found = std::find_if(
        statuses.begin(), statuses.end(),
        [&client_row](const aurora::runtime::ExternalClientStatus& status) {
          return status.id == client_row.id;
        });
    if (found == statuses.end()) continue;
    gtk_label_set_text(client_row.state, ClientStateText(*found));
    std::string subtitle = found->detail;
    if (found->pid > 0) {
      subtitle += " (PID " + std::to_string(found->pid) + ")";
    }
    adw_action_row_set_subtitle(ADW_ACTION_ROW(client_row.row),
                                subtitle.c_str());
  }
  return G_SOURCE_CONTINUE;
}

int Main() {
  gtk_init();
  adw_init();
  adw_style_manager_set_color_scheme(adw_style_manager_get_default(),
                                     ADW_COLOR_SCHEME_FORCE_DARK);
  gtk_window_set_default_icon_name("space.bigrat.aurora");
  const std::filesystem::path executable = ExecutablePath();
  State state;
  state.repo = executable.empty() ? std::filesystem::current_path()
                                  : executable.parent_path().parent_path();
  state.config = ConfigPath();
  state.loop = g_main_loop_new(nullptr, FALSE);

  GtkWidget* window = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(window), "Aurora Settings");
  gtk_window_set_default_size(GTK_WINDOW(window), 560, 660);
  g_signal_connect_swapped(window, "destroy", G_CALLBACK(g_main_loop_quit), state.loop);

  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
  gtk_widget_set_margin_top(box, 26); gtk_widget_set_margin_bottom(box, 24);
  gtk_widget_set_margin_start(box, 28); gtk_widget_set_margin_end(box, 28);
  GtkWidget* title = gtk_label_new("Aurora Settings");
  gtk_widget_add_css_class(title, "title-1"); gtk_widget_set_halign(title, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(box), title);
  GtkWidget* subtitle = gtk_label_new("เปิดหรือปิดตัวเลือกได้ตามต้องการ");
  gtk_widget_set_halign(subtitle, GTK_ALIGN_START); gtk_box_append(GTK_BOX(box), subtitle);
  GtkWidget* scroll_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
  const auto add_switch = [&](GtkWidget* group, AdwSwitchRow** slot,
                              const char* title, const char* subtitle,
                              bool active) {
    *slot = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(*slot), title);
    if (subtitle != nullptr) {
      adw_action_row_set_subtitle(ADW_ACTION_ROW(*slot), subtitle);
    }
    adw_switch_row_set_active(*slot, active);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), GTK_WIDGET(*slot));
  };
  gtk_box_append(GTK_BOX(scroll_content), SectionLabel("Features"));
  GtkWidget* features = adw_preferences_group_new();
  add_switch(features, &state.controller, "Allow Controller Access",
             "Recommended for Steam Deck and similar devices.", FALSE);
  state.controller_arrow = AddDropdownArrow(state.controller, FALSE);
  add_switch(features, &state.controller_extra, "Use Controller Input",
             "Expose controller input to Roblox.", TRUE);
  gtk_widget_set_visible(GTK_WIDGET(state.controller_extra), FALSE);
  gtk_widget_set_margin_start(GTK_WIDGET(state.controller_extra), 18);
  add_switch(features, &state.console, "Use Console Experience",
             "Show UI features optimized for console. May break certain parts of the home page.", FALSE);
  add_switch(features, &state.share_activity, "Share Activity on Discord",
             "Display your game status on your Discord profile.", FALSE);
  state.discord_arrow = AddDropdownArrow(state.share_activity, FALSE);
  add_switch(features, &state.discord_place, "Show Experience Name",
             "Include the current Roblox experience in Discord activity.", TRUE);
  gtk_widget_set_visible(GTK_WIDGET(state.discord_place), FALSE);
  gtk_widget_set_margin_start(GTK_WIDGET(state.discord_place), 18);
  add_switch(features, &state.discord_elapsed, "Show Elapsed Time",
             "Include the current session duration in Discord activity.", TRUE);
  gtk_widget_set_visible(GTK_WIDGET(state.discord_elapsed), FALSE);
  gtk_widget_set_margin_start(GTK_WIDGET(state.discord_elapsed), 18);
  add_switch(features, &state.discord_join, "Allow joining off my Discord profile",
             "Allows anyone to join your game off your Discord profile via the 'Join Game' button.", TRUE);
  gtk_box_append(GTK_BOX(scroll_content), features);
  gtk_box_append(GTK_BOX(scroll_content), SectionLabel("Online Services"));
  GtkWidget* online = adw_preferences_group_new();
  add_switch(online, &state.download, "Download Roblox",
             "Your device will download Roblox automatically when needed.", TRUE);
  GtkWidget* works = adw_action_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(works), "Aurora และ Roblox ทำงานร่วมกัน");
  adw_action_row_set_subtitle(ADW_ACTION_ROW(works), "ใช้บริการออนไลน์ของ Roblox สำหรับอัปเดตและการเข้าเกม");
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(online), works);
  gtk_box_append(GTK_BOX(scroll_content), online);
  gtk_box_append(GTK_BOX(scroll_content),
                 SectionLabel("Roblox Studio Agentic Clients"));
  GtkWidget* clients = adw_preferences_group_new();
  AddClientRow(&state, clients, "antigravity", "Antigravity");
  AddClientRow(&state, clients, "codex_cli", "Codex CLI");
  AddClientRow(&state, clients, "claude_code", "Claude Code");
  AddClientRow(&state, clients, "claude_desktop", "Claude Desktop");
  AddClientRow(&state, clients, "cursor", "Cursor");
  AddClientRow(&state, clients, "gemini_cli", "Gemini CLI");
  AddClientRow(&state, clients, "visual_studio_code", "Visual Studio Code");
  AddClientRow(&state, clients, "codex_desktop", "Codex Desktop / ChatGPT");
  AddClientRow(&state, clients, "roblox_studio_mcp", "Roblox Studio MCP");
  gtk_box_append(GTK_BOX(scroll_content), clients);
  gtk_box_append(GTK_BOX(scroll_content), SectionLabel("Behavior"));
  GtkWidget* behavior = adw_preferences_group_new();
  add_switch(behavior, &state.hidpi, "HiDPI Display Scaling Support",
             "Wayland only, may cause broken mouse input on X11 - Enable this to make app content larger based on your display and preferences.", FALSE);
  add_switch(behavior, &state.touch, "Enable Touch Input",
             "Recommended for tablets or similar.", FALSE);
  state.touch_arrow = AddDropdownArrow(state.touch, FALSE);
  add_switch(behavior, &state.mobile_experience, "Use Mobile Experience",
             "If disabled, you can still use touch input, but experiences won't display mobile-only interface elements, like certain buttons or outlines.", FALSE);
  gtk_widget_set_visible(GTK_WIDGET(state.mobile_experience), FALSE);
  gtk_widget_set_margin_start(GTK_WIDGET(state.mobile_experience), 18);
  add_switch(behavior, &state.mobile_home, "Use Mobile Home Screen",
             "Disables PC games. If disabled, you can still use touch input, but the Roblox Home Screen may have visual bugs.", FALSE);
  gtk_widget_set_visible(GTK_WIDGET(state.mobile_home), FALSE);
  gtk_widget_set_margin_start(GTK_WIDGET(state.mobile_home), 18);
  add_switch(behavior, &state.legacy, "Force Legacy Rendering",
             "Use the outdated OpenGL renderer. May fix crashes or performance issues on certain systems, such as Chromebooks.", FALSE);
  add_switch(behavior, &state.vsync, "Enable VSync",
             "Synchronize frames to the display refresh rate.", TRUE);
  add_switch(behavior, &state.threaded, "Multithreaded Rendering",
             "Use additional CPU cores for Roblox rendering work.", FALSE);
  add_switch(behavior, &state.gamemode, "Enable Gamemode",
             "Improves performance. May reduce battery life.", TRUE);
  add_switch(behavior, &state.server_location, "Show Server Location Indicator",
             "Shows an in-app toast when joining a new server.", TRUE);
  add_switch(behavior, &state.close_on_leave, "Close On Leave",
             "Closes Roblox when leaving a game.", FALSE);
  gtk_box_append(GTK_BOX(scroll_content), behavior);
  g_signal_connect(state.controller, "notify::active", G_CALLBACK(OnControllerToggled), &state);
  g_signal_connect(state.share_activity, "notify::active", G_CALLBACK(OnDiscordToggled), &state);
  g_signal_connect(state.touch, "notify::active", G_CALLBACK(OnTouchToggled), &state);
  g_signal_connect(state.controller_arrow, "clicked", G_CALLBACK(OnControllerArrowClicked), &state);
  g_signal_connect(state.discord_arrow, "clicked", G_CALLBACK(OnDiscordArrowClicked), &state);
  g_signal_connect(state.touch_arrow, "clicked", G_CALLBACK(OnTouchArrowClicked), &state);
  GtkWidget* scroll = gtk_scrolled_window_new();
  gtk_widget_set_vexpand(scroll, TRUE); gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), scroll_content);
  gtk_box_append(GTK_BOX(box), scroll);
  GtkWidget* note = gtk_label_new("ค่าจะมีผลเมื่อเปิด Aurora ครั้งถัดไป");
  gtk_widget_set_halign(note, GTK_ALIGN_START); gtk_widget_add_css_class(note, "dim-label"); gtk_box_append(GTK_BOX(box), note);
  state.status = GTK_LABEL(gtk_label_new("")); gtk_widget_set_halign(GTK_WIDGET(state.status), GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(box), GTK_WIDGET(state.status));
  GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10); gtk_widget_set_halign(actions, GTK_ALIGN_END);
  GtkWidget* close = gtk_button_new_with_label("ปิด");
  GtkWidget* save = gtk_button_new_with_label("บันทึก"); gtk_widget_add_css_class(save, "suggested-action");
  GtkWidget* play = gtk_button_new_with_label("บันทึกและเล่น Roblox"); gtk_widget_add_css_class(play, "suggested-action");
  g_signal_connect(close, "clicked", G_CALLBACK(Close), &state);
  g_signal_connect(save, "clicked", G_CALLBACK(Save), &state);
  g_signal_connect(play, "clicked", G_CALLBACK(Play), &state);
  gtk_box_append(GTK_BOX(actions), close); gtk_box_append(GTK_BOX(actions), save); gtk_box_append(GTK_BOX(actions), play);
  gtk_box_append(GTK_BOX(box), actions); gtk_window_set_child(GTK_WINDOW(window), box);
  RefreshClients(&state);
  state.client_poll_source = g_timeout_add_seconds(1, RefreshClients, &state);
  gtk_window_present(GTK_WINDOW(window)); g_main_loop_run(state.loop);
  if (state.client_poll_source != 0) {
    g_source_remove(state.client_poll_source);
    state.client_poll_source = 0;
  }
  g_main_loop_unref(state.loop); return 0;
}
}  // namespace
int main() { return Main(); }
