#pragma once

/*
Thanks to @krys-spectralpixel and @Flix01 for this code
https://github.com/ocornut/imgui/issues/261
*/

namespace ImGui {
IMGUI_API  bool TabLabels(int numTabs, const char** tabLabels, int *selectedIndex, const char** tabLabelTooltips, bool autoLayout, int *pOptionalHoveredIndex) {
	ImGuiStyle& style = ImGui::GetStyle();

	const ImVec2 itemSpacing =  style.ItemSpacing;
	const ImVec4 color =        style.Colors[ImGuiCol_Button];
	const ImVec4 colorActive =  style.Colors[ImGuiCol_ButtonActive];
	const ImVec4 colorHover =   style.Colors[ImGuiCol_ButtonHovered];
	style.ItemSpacing.x =       1;
	style.ItemSpacing.y =       1;


	if (numTabs > 0 && (*selectedIndex < 0 || *selectedIndex >= numTabs)) *selectedIndex = 0;
	if (pOptionalHoveredIndex) *pOptionalHoveredIndex = -1;

	// Parameters to adjust to make autolayout work as expected:----------
	// The correct values are probably the ones in the comments, but I took some margin so that they work well
	// with a (medium size) vertical scrollbar too [Ok I should detect its presence and use the appropriate values...].
	const float btnOffset =         2.f * style.FramePadding.x; // [2.f*style.FramePadding.x] It should be: ImGui::Button(text).size.x = ImGui::CalcTextSize(text).x + btnOffset;
	const float sameLineOffset =    2.f * style.ItemSpacing.x;  // [style.ItemSpacing.x]      It should be: sameLineOffset = ImGui::SameLine().size.x;
	const float uniqueLineOffset =  2.f * style.WindowPadding.x; // [style.WindowPadding.x]    Width to be sutracted by windowWidth to make it work.
	//--------------------------------------------------------------------

	float windowWidth = 0.f, sumX = 0.f;
	if (autoLayout) windowWidth = ImGui::GetWindowWidth() - uniqueLineOffset;

	bool selection_changed = false;
	for (int i = 0; i < numTabs; i++) {
		// push the style
		if (i == *selectedIndex) {
			style.Colors[ImGuiCol_Button] =         colorActive;
			style.Colors[ImGuiCol_ButtonActive] =   colorActive;
			style.Colors[ImGuiCol_ButtonHovered] =  colorActive;
		}
		else {
			style.Colors[ImGuiCol_Button] =         color;
			style.Colors[ImGuiCol_ButtonActive] =   colorActive;
			style.Colors[ImGuiCol_ButtonHovered] =  colorHover;
		}

		ImGui::PushID(i);   // otherwise two tabs with the same name would clash.

		if (!autoLayout) {if (i > 0) ImGui::SameLine();}
		else if (sumX > 0.f) {
			sumX += sameLineOffset; // Maybe we can skip it if we use SameLine(0,0) below
			sumX += ImGui::CalcTextSize(tabLabels[i]).x + btnOffset;
			if (sumX > windowWidth) sumX = 0.f;
			else ImGui::SameLine();
		}

		// Draw the button
		if (ImGui::Button(tabLabels[i]))   {selection_changed = (*selectedIndex != i); *selectedIndex = i;}
		if (autoLayout && sumX == 0.f) {
			// First element of a line
			sumX = ImGui::GetItemRectSize().x;
		}
		if (pOptionalHoveredIndex) {
			if (ImGui::IsItemHovered()) {
				*pOptionalHoveredIndex = i;
				if (tabLabelTooltips && tabLabelTooltips[i] && strlen(tabLabelTooltips[i]) > 0)  ImGui::SetTooltip("%s", tabLabelTooltips[i]);
			}
		}
		else if (tabLabelTooltips && tabLabelTooltips[i] && ImGui::IsItemHovered() && strlen(tabLabelTooltips[i]) > 0) ImGui::SetTooltip("%s", tabLabelTooltips[i]);
		ImGui::PopID();
	}

	// Restore the style
	style.Colors[ImGuiCol_Button] =         color;
	style.Colors[ImGuiCol_ButtonActive] =   colorActive;
	style.Colors[ImGuiCol_ButtonHovered] =  colorHover;
	style.ItemSpacing =                     itemSpacing;

	return selection_changed;
}
} // namespace ImGui
