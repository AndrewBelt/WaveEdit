#include "WaveEdit.hpp"

#include "imgui.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_internal.h"




void waveLine(float *points, int pointsLen, float startIndex, float endIndex, float startValue, float endValue) {
	// Switch indices if out of order
	if (startIndex > endIndex) {
		float tmpIndex = startIndex;
		startIndex = endIndex;
		endIndex = tmpIndex;
		float tmpValue = startValue;
		startValue = endValue;
		endValue = tmpValue;
	}

	int startI = maxi(0, roundf(startIndex));
	int endI = mini(pointsLen - 1, roundf(endIndex));
	for (int i = startI; i <= endI; i++) {
		float frac = (startIndex < endIndex) ? rescalef(i, startIndex, endIndex, 0.0, 1.0) : 0.0;
		points[i] = crossf(startValue, endValue, frac);
	}
}


void waveSmooth(float *points, int pointsLen, float index) {
	// TODO
	for (int i = 0; i <= pointsLen - 1; i++) {
		const float a = 0.05;
		float w = expf(-a * powf(i - index, 2.0));
		points[i] = clampf(points[i] + 0.01 * w, -1.0, 1.0);
	}
}


void waveBrush(float *points, int pointsLen, float startIndex, float endIndex, float startValue, float endValue) {
	const float sigma = 10.0;
	for (int i = 0; i < pointsLen; i++) {
		float x = i - startIndex;
		// float a;
		float a = expf(-x * x / (2.0 * sigma));
		points[i] = crossf(points[i], startValue, a);
	}
}


bool editorBehavior(ImGuiID id, const ImRect& box, const ImRect& inner, float *points, int pointsLen, float minIndex, float maxIndex, float minValue, float maxValue, enum Tool tool) {
	ImGuiContext &g = *GImGui;
	ImGuiWindow *window = ImGui::GetCurrentWindow();

	bool hovered = ImGui::IsHovered(box, id);
	if (hovered) {
		ImGui::SetHoveredID(id);
		if (g.IO.MouseClicked[0]) {
			ImGui::SetActiveID(id, window);
			ImGui::FocusWindow(window);
			g.ActiveIdClickOffset = g.IO.MousePos - box.Min;
		}
	}

	// Unhover
	if (g.ActiveId == id) {
		if (!g.IO.MouseDown[0]) {
			ImGui::ClearActiveID();
		}
	}

	// Tool behavior
	if (g.ActiveId == id) {
		if (g.IO.MouseDown[0]) {
			if (tool == NO_TOOL)
				return true;

			ImVec2 pos = g.IO.MousePos;
			ImVec2 lastPos = g.IO.MousePos - g.IO.MouseDelta;
			ImVec2 originalPos = g.ActiveIdClickOffset + box.Min;
			float originalIndex = rescalef(originalPos.x, inner.Min.x, inner.Max.x, minIndex, maxIndex);
			float originalValue = rescalef(originalPos.y, inner.Min.y, inner.Max.y, minValue, maxValue);
			float lastIndex = rescalef(lastPos.x, inner.Min.x, inner.Max.x, minIndex, maxIndex);
			float lastValue = rescalef(lastPos.y, inner.Min.y, inner.Max.y, minValue, maxValue);
			float index = rescalef(pos.x, inner.Min.x, inner.Max.x, minIndex, maxIndex);
			float value = rescalef(pos.y, inner.Min.y, inner.Max.y, minValue, maxValue);

			// Pencil tool
			if (tool == PENCIL_TOOL) {
				waveLine(points, pointsLen, lastIndex, index, lastValue, value);
			}
			// Grab tool
			else if (tool == GRAB_TOOL) {
				waveLine(points, pointsLen, originalIndex, originalIndex, value, value);
			}
			// Brush tool
			else if (tool == BRUSH_TOOL) {
				waveBrush(points, pointsLen, lastIndex, index, lastValue, value);
			}
			// Line tool
			else if (tool == LINE_TOOL) {
				// TODO Restore points from when it was originally clicked, using undo history
				waveLine(points, pointsLen, originalIndex, index, originalValue, value);
			}
			// Eraser tool
			else if (tool == ERASER_TOOL) {
				waveLine(points, pointsLen, lastIndex, index, 0.0, 0.0);
			}
			// Smooth tool
			else if (tool == SMOOTH_TOOL) {
				waveSmooth(points, pointsLen, lastIndex);
			}

			for (int i = 0; i < pointsLen; i++) {
				points[i] = clampf(points[i], fminf(minValue, maxValue), fmaxf(minValue, maxValue));
			}

			return true;
		}
	}

	return false;
}


bool renderWave(const char *name, float height, float *points, int pointsLen, const float *lines, int linesLen, enum Tool tool) {
	ImGuiContext &g = *GImGui;
	ImGuiWindow *window = ImGui::GetCurrentWindow();
	const ImGuiStyle &style = g.Style;
	const ImGuiID id = window->GetID(name);

	// Compute positions
	ImVec2 size = ImVec2(ImGui::CalcItemWidth(), height);
	ImRect box = ImRect(window->DC.CursorPos, window->DC.CursorPos + size);
	ImRect inner = ImRect(box.Min + style.FramePadding, box.Max - style.FramePadding);
	ImGui::ItemSize(box, style.FramePadding.y);
	if (!ImGui::ItemAdd(box, NULL))
		return false;

	// Draw frame
	ImGui::RenderFrame(box.Min, box.Max, ImGui::GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);

	// // Tooltip
	// if (ImGui::IsHovered(box, 0)) {
	// 	ImVec2 mousePos = ImGui::GetMousePos();
	// 	float x = rescalef(mousePos.x, inner.Min.x, inner.Max.x, 0, pointsLen-1);
	// 	int xi = (int)clampf(x, 0, pointsLen-2);
	// 	float xf = x - xi;
	// 	float y = crossf(values[xi], values[xi+1], xf);
	// 	ImGui::SetTooltip("%f, %f\n", x, y);
	// }

	// const bool hovered = ImGui::IsHovered(box, id);
	// if (hovered) {
	// 	ImGui::SetHoveredID(id);
	// 	ImGui::SetActiveID(id, window);
	// }

	bool edited = editorBehavior(id, box, inner, points, pointsLen, 0.0, pointsLen - 1, 1.0, -1.0, tool);

	ImGui::PushClipRect(box.Min, box.Max, true);
	ImVec2 lastPos;
	// Draw lines
	if (lines) {
		for (int i = 0; i < linesLen; i++) {
			ImVec2 pos = ImVec2(rescalef(i, 0, linesLen - 1, inner.Min.x, inner.Max.x), rescalef(lines[i], 1.0, -1.0, inner.Min.y, inner.Max.y));
			if (i > 0)
				window->DrawList->AddLine(lastPos, pos, ImGui::GetColorU32(ImGuiCol_PlotLines));
			lastPos = pos;
		}
	}
	// Draw points
	if (points) {
		for (int i = 0; i < pointsLen; i++) {
			ImVec2 pos = ImVec2(rescalef(i, 0, pointsLen - 1, inner.Min.x, inner.Max.x), rescalef(points[i], 1.0, -1.0, inner.Min.y, inner.Max.y));
			window->DrawList->AddCircleFilled(pos + ImVec2(0.5, 0.5), 2.0, ImGui::GetColorU32(ImGuiCol_PlotLines), 12);
		}
	}
	ImGui::PopClipRect();

	return edited;
}


bool renderHistogram(const char *name, float height, float *points, int pointsLen, const float *lines, int linesLen, enum Tool tool) {
	ImGuiContext &g = *GImGui;
	ImGuiWindow *window = ImGui::GetCurrentWindow();
	const ImGuiStyle &style = g.Style;
	const ImGuiID id = window->GetID(name);

	// Compute positions
	ImVec2 pos = window->DC.CursorPos;
	ImVec2 size = ImVec2(ImGui::CalcItemWidth(), height);
	ImRect box = ImRect(pos, pos + size);
	ImRect inner = ImRect(box.Min + style.FramePadding, box.Max - style.FramePadding);
	ImGui::ItemSize(box, style.FramePadding.y);
	if (!ImGui::ItemAdd(box, NULL))
		return false;

	bool edited = editorBehavior(id, box, inner, points, pointsLen, -0.5, pointsLen - 0.5, 1.0, 0.0, tool);

	// Draw frame
	ImGui::RenderFrame(box.Min, box.Max, ImGui::GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);

	// Draw bars
	ImGui::PushClipRect(box.Min, box.Max, true);
	const float rounding = 0.0;
	for (int i = 0; i < pointsLen; i++) {
		float value = points[i];
		ImVec2 pos0 = ImVec2(rescalef(i, 0, pointsLen, inner.Min.x, inner.Max.x), rescalef(value, 0.0, 1.0, inner.Max.y, inner.Min.y));
		ImVec2 pos1 = ImVec2(rescalef(i + 1, 0, pointsLen, inner.Min.x, inner.Max.x) - 1, inner.Max.y);
		window->DrawList->AddRectFilled(pos0, pos1, ImGui::GetColorU32(ImVec4(1.0, 0.8, 0.2, 1.0)), rounding);
	}
	for (int i = 0; i < linesLen; i++) {
		float value = lines[i];
		ImVec2 pos0 = ImVec2(rescalef(i, 0, linesLen, inner.Min.x, inner.Max.x), rescalef(value, 0.0, 1.0, inner.Max.y, inner.Min.y));
		ImVec2 pos1 = ImVec2(rescalef(i + 1, 0, linesLen, inner.Min.x, inner.Max.x) - 1, inner.Max.y);
		window->DrawList->AddRectFilled(pos0, pos1, ImGui::GetColorU32(ImVec4(0.9, 0.7, 0.1, 0.75)), rounding);
	}
	ImGui::PopClipRect();

	return edited;
}


bool renderWaveGrid(const char *name, int gridWidth, int gridHeight, float **lines, int linesLen, float *gridX, float *gridY) {
	ImVec2 padding = ImGui::GetStyle().FramePadding;
	ImVec2 windowPadding = ImGui::GetStyle().WindowPadding;
	ImVec2 size = ImGui::GetWindowSize() - windowPadding - padding;
	ImRect box = ImRect(ImGui::GetCursorScreenPos(), ImGui::GetCursorScreenPos() + size);
	ImVec2 cellSize = ImVec2((size.x + padding.x) / gridWidth, (size.y + padding.y) / gridHeight);
	ImGui::ItemSize(box, ImGui::GetStyle().FramePadding.y);
	if (!ImGui::ItemAdd(box, NULL))
		return false;
	bool edited = false;

	// Wave grid
	for (int j = 0; j < gridWidth * gridHeight; j++) {
		int x = j % gridWidth;
		int y = j / gridWidth;
		// Compute cell box
		ImVec2 cellPos = ImVec2(box.Min.x + cellSize.x * x, box.Min.y + cellSize.y * y);
		ImRect cellBox = ImRect(cellPos, cellPos + cellSize - padding);
		ImGui::RenderFrame(cellBox.Min, cellBox.Max, ImGui::GetColorU32(ImGuiCol_FrameBg), true, ImGui::GetStyle().FrameRounding);

		// Draw lines
		ImGui::PushClipRect(cellBox.Min, cellBox.Max, true);
		if (lines) {
			ImVec2 lastPos;
			for (int i = 0; i < linesLen; i++) {
				float value = lines[j][i];
				ImVec2 pos = ImVec2(rescalef(i, 0, linesLen - 1, cellBox.Min.x, cellBox.Max.x), rescalef(value, 1.0, -1.0, cellBox.Min.y, cellBox.Max.y));
				if (i > 0)
					ImGui::GetWindowDrawList()->AddLine(lastPos, pos, ImGui::GetColorU32(ImGuiCol_PlotLines));
				lastPos = pos;
			}
		}
		ImGui::PopClipRect();
	}

	// Cursor circle
	if (gridX && gridY) {
		// Draw circle
		ImVec2 circlePos = ImVec2(rescalef(*gridX, 0.0, gridWidth, box.Min.x, box.Max.x), rescalef(*gridY, 0.0, gridHeight, box.Min.y, box.Max.y)) + cellSize / 2.0;
		ImGui::GetWindowDrawList()->AddCircleFilled(circlePos, 8.0, ImGui::GetColorU32(ImGuiCol_SliderGrab), 24);

		// Get mouse input
		if (ImGui::IsMouseHoveringWindow() && ImGui::IsMouseDown(0)) {
			ImVec2 pos = ImGui::GetIO().MousePos - cellSize / 2.0;
			*gridX = clampf(rescalef(pos.x, box.Min.x, box.Max.x, 0.0, gridWidth), 0, gridWidth - 1);
			*gridY = clampf(rescalef(pos.y, box.Min.y, box.Max.y, 0.0, gridHeight), 0, gridHeight - 1);
		}

		if (ImGui::IsMouseHoveringWindow() && ImGui::IsMouseClicked(1)) {
			ImGui::OpenPopup("Grid Context Menu");
		}
	}

	// Context menu
	if (ImGui::BeginPopup("Grid Context Menu")) {
		if (ImGui::MenuItem("Copy")) {

		}
		if (ImGui::MenuItem("Paste")) {

		}
		if (ImGui::MenuItem("Duplicate To All")) {

		}
		if (ImGui::MenuItem("Clear")) {
			edited = true;
		}
		if (ImGui::MenuItem("Clear All")) {
		}
		ImGui::EndPopup();
	}

	return edited;
}


void renderWave3D(float height, const float *const *waves, int bankLen, int waveLen) {
	ImGuiContext &g = *GImGui;
	ImGuiWindow *window = ImGui::GetCurrentWindow();
	const ImGuiStyle &style = g.Style;

	ImVec2 size = ImVec2(ImGui::CalcItemWidth(), height);
	ImRect box = ImRect(window->DC.CursorPos, window->DC.CursorPos + size);
	ImRect inner = ImRect(box.Min + style.FramePadding, box.Max - style.FramePadding);

	ImGui::PushClipRect(box.Min, box.Max, true);

	const float waveHeight = 10.0;
	ImVec2 waveOffset = ImVec2(5, -5);

	for (int b = 0; b < bankLen; b++) {
		ImVec2 *points = new ImVec2[waveLen];
		for (int i = 0; i < waveLen; i++) {
			float value = waves[b][i];
			points[i] = ImVec2(rescalef(i, 0, waveLen - 1, inner.Min.x, inner.Max.x), rescalef(value, -1.0, 1.0, inner.Max.y - 200 + waveHeight, inner.Max.y - 200 - waveHeight) + 0.5 * i) + waveOffset * b;
		}
		window->DrawList->AddPolyline(points, waveLen, ImGui::GetColorU32(ImVec4(1.0, 0.8, 0.2, 1.0)), false, 0.1, true);
		delete[] points;
	}

	ImGui::PopClipRect();
}
