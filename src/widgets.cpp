#include "WaveEdit.hpp"

#include "imgui.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_internal.h"



static void drawGrid(ImRect inner, int len) {
	ImGuiWindow *window = ImGui::GetCurrentWindow();
	// Compute number of points to skip, should be a power of 2
	int skip;
	for (int j = 0; j < 20; j++) {
		skip = 1 << j;
		float skipX = (inner.Max.x - inner.Min.x) / len * skip;
		if (fabsf(skipX) >= 22.0)
			break;
	}

	for (int i = 0; i <= len; i += skip) {
		float gridX = rescalef(i, 0, len, inner.Min.x, inner.Max.x);
		// Grid line
		int thicknessIndex = i / skip;
		float thickness;
		if (thicknessIndex % 64 == 0)
			thickness = 3.0;
		else if (thicknessIndex % 8 == 0)
			thickness = 2.0;
		else
			thickness = 1.0;
		window->DrawList->AddLine(ImVec2(gridX, inner.Min.y), ImVec2(gridX, inner.Max.y), ImGui::GetColorU32(ImGuiCol_WindowBg), thickness);
		// Text
		if (i < len) {
			char label[64];
			snprintf(label, sizeof(label), "%d", i);
			ImVec2 labelPos = ImVec2(gridX, inner.Min.y) + ImVec2(4, -1);
			window->DrawList->AddText(labelPos, ImGui::GetColorU32(ImGuiCol_PlotLines), label);
		}
	}
}

static void waveLine(float *points, int pointsLen, float startIndex, float endIndex, float startValue, float endValue) {
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


static void waveSmooth(float *points, int pointsLen, float index) {
	// TODO
	for (int i = 0; i <= pointsLen - 1; i++) {
		const float a = 0.05;
		float w = expf(-a * powf(i - index, 2.0));
		points[i] = clampf(points[i] + 0.01 * w, -1.0, 1.0);
	}
}


static void waveBrush(float *points, int pointsLen, float startIndex, float endIndex, float startValue, float endValue) {
	const float sigma = 10.0;
	for (int i = 0; i < pointsLen; i++) {
		float x = i - startIndex;
		// float a;
		float a = expf(-x * x / (2.0 * sigma));
		points[i] = crossf(points[i], startValue, a);
	}
}


static bool editorBehavior(ImGuiID id, const ImRect& box, const ImRect& inner, float *points, int pointsLen, float minIndex, float maxIndex, float minValue, float maxValue, enum Tool tool) {
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
	// Draw lines
	if (lines) {
		ImVec2 lastPos;
		for (int i = 0; i < linesLen; i++) {
			ImVec2 pos = ImVec2(rescalef(i, 0, linesLen, inner.Min.x, inner.Max.x), rescalef(lines[i], 1.0, -1.0, inner.Min.y, inner.Max.y));
			if (i > 0)
				window->DrawList->AddLine(lastPos, pos, ImGui::GetColorU32(ImGuiCol_PlotLines));
			lastPos = pos;
		}
	}
	// Draw points
	if (points) {
		for (int i = 0; i < pointsLen; i++) {
			ImVec2 pos = ImVec2(rescalef(i, 0, pointsLen, inner.Min.x, inner.Max.x), rescalef(points[i], 1.0, -1.0, inner.Min.y, inner.Max.y));
			window->DrawList->AddCircleFilled(pos + ImVec2(0.5, 0.5), 2.0, ImGui::GetColorU32(ImGuiCol_PlotLines), 12);
		}
	}
	// Draw grid
	drawGrid(inner, pointsLen);

	ImGui::PopClipRect();

	return edited;
}


bool renderHistogram(const char *name, float height, float *bars, int barsLen, const float *ghost, int ghostLen, enum Tool tool) {
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

	bool edited = editorBehavior(id, box, inner, bars, barsLen, -0.5, barsLen - 0.5, 1.0, 0.0, tool);

	// Draw frame
	ImGui::RenderFrame(box.Min, box.Max, ImGui::GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);

	// Draw bars
	ImGui::PushClipRect(box.Min, box.Max, true);
	const float rounding = 0.0;
	for (int i = 0; i < barsLen; i++) {
		float value = bars[i];
		ImVec2 pos0 = ImVec2(rescalef(i, 0, barsLen, inner.Min.x, inner.Max.x), rescalef(value, 0.0, 1.0, inner.Max.y, inner.Min.y));
		ImVec2 pos1 = ImVec2(rescalef(i + 1, 0, barsLen, inner.Min.x, inner.Max.x) - 1, inner.Max.y);
		window->DrawList->AddRectFilled(pos0, pos1, ImGui::GetColorU32(ImGuiCol_PlotHistogram), rounding);
	}
	// Draw ghost bars
	for (int i = 0; i < ghostLen; i++) {
		float value = ghost[i];
		ImVec2 pos0 = ImVec2(rescalef(i, 0, ghostLen, inner.Min.x, inner.Max.x), rescalef(value, 0.0, 1.0, inner.Max.y, inner.Min.y));
		ImVec2 pos1 = ImVec2(rescalef(i + 1, 0, ghostLen, inner.Min.x, inner.Max.x) - 1, inner.Max.y);
		window->DrawList->AddRectFilled(pos0, pos1, ImGui::GetColorU32(ImGuiCol_PlotHistogramHovered), rounding);
	}
	// Draw grid
	drawGrid(inner, barsLen);

	ImGui::PopClipRect();

	return edited;
}


static void waveMenu() {
	if (ImGui::BeginPopup("Wave Menu")) {
		renderWaveMenu();
		ImGui::MenuItem("##spacer", NULL, false, false);
		ImGui::MenuItem("(Bank)", NULL, false, false);
		if (ImGui::MenuItem("Duplicate To All")) {
			currentBank.duplicateToAll(selectedId);
			historyPush();
		}
		if (ImGui::MenuItem("Shuffle Bank")) {
			currentBank.shuffle();
			historyPush();
		}
		if (ImGui::MenuItem("Clear All")) {
			currentBank.clear();
			historyPush();
		}
		ImGui::EndPopup();
	}
}


void renderBankGrid(const char *name, float height, int gridWidth, float *gridX, float *gridY) {
	assert(BANK_LEN % gridWidth == 0);
	int gridHeight = BANK_LEN / gridWidth;

	ImGuiContext &g = *GImGui;
	ImGuiWindow *window = ImGui::GetCurrentWindow();
	const ImGuiStyle &style = g.Style;
	const ImGuiID id = window->GetID(name);
	ImVec2 padding = style.FramePadding;
	ImVec2 windowPadding = style.WindowPadding;

	if (height < 0.f)
		height = ImGui::GetWindowSize().y - windowPadding.y - padding.y;
	ImVec2 size = ImVec2(ImGui::CalcItemWidth(), height);
	ImRect box = ImRect(window->DC.CursorPos, window->DC.CursorPos + size);
	ImVec2 cellSize = ImVec2((size.x + padding.x) / gridWidth, (size.y + padding.y) / gridHeight);
	ImGui::ItemSize(box, style.FramePadding.y);
	if (!ImGui::ItemAdd(box, NULL))
		return;

	// Wave grid
	int selectedStart = mini(selectedId, lastSelectedId);
	int selectedEnd = maxi(selectedId, lastSelectedId);
	for (int j = 0; j < BANK_LEN; j++) {
		int x = j % gridWidth;
		int y = j / gridWidth;
		// Compute cell box
		ImVec2 cellPos = ImVec2(box.Min.x + cellSize.x * x, box.Min.y + cellSize.y * y);
		ImRect cellBox = ImRect(cellPos, cellPos + cellSize - padding);
		ImU32 col = ImGui::GetColorU32(ImGuiCol_FrameBg);
		if (selectedStart <= j && j <= selectedEnd) {
			col = ImGui::GetColorU32(ImGuiCol_WindowBg);
		}
		ImGui::RenderFrame(cellBox.Min, cellBox.Max, col, true, ImGui::GetStyle().FrameRounding);

		// Draw lines
		ImGui::PushClipRect(cellBox.Min, cellBox.Max, true);
		ImVec2 lastPos;
		for (int i = 0; i < WAVE_LEN; i++) {
			float value = currentBank.waves[j].postSamples[i];
			float margin = 3.0;
			ImVec2 pos = ImVec2(rescalef(i, 0, WAVE_LEN - 1, cellBox.Min.x, cellBox.Max.x), rescalef(value, 1.0, -1.0, cellBox.Min.y + margin, cellBox.Max.y - margin));
			if (i > 0)
				window->DrawList->AddLine(lastPos, pos, ImGui::GetColorU32(ImGuiCol_PlotLines));
			lastPos = pos;
		}

		// Draw cell label
		char label[64];
		snprintf(label, sizeof(label), "%d", j);
		ImVec2 labelPos = cellPos + ImVec2(2, 2);
		window->DrawList->AddText(labelPos, ImGui::GetColorU32(ImGuiCol_PlotLines), label);
		ImGui::PopClipRect();
	}

	// Behavior
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

	// Select wave if left-dragged or right-clicked
	ImVec2 gridPos;
	if (gridX && gridY) {
		gridPos.x = *gridX;
		gridPos.y = *gridY;
	}

	if ((g.ActiveId == id && id && g.IO.MouseDown[0]) || (hovered && g.IO.MouseClicked[1])) {
		ImVec2 cellPos = g.IO.MousePos - cellSize / 2.0;
		gridPos.x = clampf(rescalef(cellPos.x, box.Min.x, box.Max.x, 0.0, gridWidth), 0, gridWidth - 1);
		gridPos.y = clampf(rescalef(cellPos.y, box.Min.y, box.Max.y, 0.0, gridHeight), 0, gridHeight - 1);
		// Block select
		int clickedId = (int)roundf(gridPos.y) * gridWidth + (int)roundf(gridPos.x);

		// Ctrl-click dragging buffers
		static Bank dragBank;
		static Wave dragWaves[BANK_LEN];
		static int dragId, dragStart, dragEnd;
		if (g.IO.KeyCtrl && !g.IO.MouseReleased[0]) {
			if (g.IO.MouseClicked[0]) {
				dragBank = currentBank;
				dragId = clickedId;
				dragStart = selectedStart;
				dragEnd = selectedEnd;
				for (int i = dragStart; i <= dragEnd; i++) {
					dragWaves[i] = currentBank.waves[i];
				}
			}
			else {
				int offsetId = clickedId - dragId;
				currentBank = dragBank;
				for (int i = dragStart; i <= dragEnd; i++) {
					int j = i + offsetId;
					if (0 <= j && j < BANK_LEN)
						currentBank.waves[j] = dragWaves[i];
				}
				// Move selection
				selectedId = clampi(dragStart + offsetId, 0, BANK_LEN-1);
				lastSelectedId = clampi(dragEnd + offsetId, 0, BANK_LEN-1);
			}
		}
		else if (g.IO.MouseClicked[1]) {
			if (selectedStart <= clickedId && clickedId <= selectedEnd) {
			}
			else {
				selectedId = clickedId;
				lastSelectedId = clickedId;
			}
		}
		else {
			// Select block
			if (g.IO.KeyShift) {
				lastSelectedId = clickedId;
			}
			if (!g.IO.KeyShift) {
				selectedId = clickedId;
				lastSelectedId = clickedId;
			}
		}

		// Update grid (cursor) position
		if (g.IO.MouseDoubleClicked[0]) {
			// Round gridPos to integers
			gridPos.x = roundf(gridPos.x);
			gridPos.y = roundf(gridPos.y);
			ImGui::ClearActiveID();
		}

		if (!g.IO.MouseClicked[1] && (gridX && gridY)) {
			*gridX = gridPos.x;
			*gridY = gridPos.y;
		}
	}

	// Cursor circle
	if (gridX && gridY) {
		ImVec2 circlePos = ImVec2(
			rescalef(*gridX, 0.0, gridWidth, box.Min.x, box.Max.x),
			rescalef(*gridY, 0.0, gridHeight, box.Min.y, box.Max.y)) + cellSize / 2.0;
		ImGui::GetWindowDrawList()->AddCircleFilled(circlePos, 8.0, ImGui::GetColorU32(ImGuiCol_ScrollbarGrab), 24);
	}

	// Right click context menu
	if (hovered && g.IO.MouseClicked[1]) {
		ImGui::OpenPopup("Wave Menu");
	}

	waveMenu();
}


void renderWaterfall(const char *name, float height, float amplitude, float angle, float *activeZ) {
	ImGuiContext &g = *GImGui;
	ImGuiWindow *window = ImGui::GetCurrentWindow();
	const ImGuiStyle &style = g.Style;
	const ImGuiID id = window->GetID(name);

	ImVec2 padding = style.FramePadding;
	ImVec2 windowPadding = style.WindowPadding;

	if (height < 0.f)
		height = ImGui::GetWindowSize().y - windowPadding.y - padding.y;
	ImVec2 size = ImVec2(ImGui::CalcItemWidth(), height);
	ImRect box = ImRect(window->DC.CursorPos, window->DC.CursorPos + size);
	ImRect inner = ImRect(box.Min + style.FramePadding, box.Max - style.FramePadding);

	ImGui::PushClipRect(box.Min, box.Max, true);

	// Behavior
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

	float theta = -M_PI / 4.0 - 2.0 * M_PI * angle;

	// ActiveZ select
	if ((g.ActiveId == id && g.IO.MouseDown[0]) || (hovered && g.IO.MouseClicked[1])) {
		ImVec2 point = g.IO.MousePos;
		ImVec2 a = ImVec2(rescalef(point.x, box.Min.x, box.Max.x, -1.0, 1.0), rescalef(point.y, box.Min.y, box.Max.y, 1.0, -1.0));
		a = ImRotate(a * M_SQRT2, cosf(-theta), sinf(-theta));
		float z = clampf(rescalef(a.y, -1.0, 1.0, 0, BANK_LEN-1), 0.0, BANK_LEN-1);
		if (g.IO.MouseClicked[1])
			z = roundf(z);
		*activeZ = z;
		selectedId = (int)roundf(z);
		lastSelectedId = selectedId;
	}

	ImVec2 waveOffset = ImVec2(5, -5);

	// Pre-effect plots
	for (int b = 0; b < BANK_LEN; b++) {
		ImVec2 points[WAVE_LEN];
		for (int i = 0; i < WAVE_LEN; i++) {
			float value = currentBank.waves[b].samples[i];
			ImVec2 a = ImVec2(rescalef(i, 0, WAVE_LEN-1, -1.0, 1.0), rescalef(b, 0, BANK_LEN-1, -1.0, 1.0));
			a = ImRotate(a, cosf(theta), sinf(theta)) / M_SQRT2;
			a.y += -amplitude * 0.3 * value;
			ImVec2 point = ImVec2(rescalef(a.x, -1.0, 1.0, box.Min.x, box.Max.x), rescalef(a.y, 1.0, -1.0, box.Min.y, box.Max.y));
			points[i] = point;
		}
		float thickness = 1.0;
		window->DrawList->AddPolyline(points, WAVE_LEN, ImGui::GetColorU32(ImGuiCol_FrameBg), false, thickness, true);
	}

	// Post-effect plots
	for (int b = 0; b < BANK_LEN; b++) {
		ImVec2 points[WAVE_LEN];
		for (int i = 0; i < WAVE_LEN; i++) {
			float value = currentBank.waves[b].postSamples[i];
			ImVec2 a = ImVec2(rescalef(i, 0, WAVE_LEN-1, -1.0, 1.0), rescalef(b, 0, BANK_LEN-1, -1.0, 1.0));
			a = ImRotate(a, cosf(theta), sinf(theta)) / M_SQRT2;
			a.y += -amplitude * 0.3 * value;
			ImVec2 point = ImVec2(rescalef(a.x, -1.0, 1.0, box.Min.x, box.Max.x), rescalef(a.y, 1.0, -1.0, box.Min.y, box.Max.y));
			points[i] = point;
		}
		float thickness = 1.0 + 4.0 * fmaxf(1.0 - fabsf(b - *activeZ), 0.0);
		window->DrawList->AddPolyline(points, WAVE_LEN, ImGui::GetColorU32(ImGuiCol_PlotHistogram), false, thickness, true);
	}

	ImGui::PopClipRect();

	// Right click context menu
	if (hovered && g.IO.MouseClicked[1]) {
		ImGui::OpenPopup("Wave Menu");
	}

	waveMenu();
}


float renderBankWave(const char *name, float height, const float *lines, int linesLen, float bankStart, float bankEnd, int bankLen) {
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
		return 0.0;

	// Behavior
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

	// Draw frame
	ImGui::RenderFrame(box.Min, box.Max, ImGui::GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);

	ImGui::PushClipRect(box.Min, box.Max, true);

	// Draw lines
	if (lines) {
		ImVec2 lastPos;
		for (int i = 0; i < linesLen; i++) {
			ImVec2 pos = ImVec2(rescalef(i, 0, linesLen, inner.Min.x, inner.Max.x), rescalef(lines[i], 1.0, -1.0, inner.Min.y, inner.Max.y));
			if (i > 0)
				window->DrawList->AddLine(lastPos, pos, ImGui::GetColorU32(ImGuiCol_PlotLines));
			lastPos = pos;
		}
	}

	// Draw grid
	ImRect gridInner = inner;
	gridInner.Min.x = rescalef(bankStart, 0, linesLen, inner.Min.x, inner.Max.x);
	gridInner.Max.x = rescalef(bankEnd, 0, linesLen, inner.Min.x, inner.Max.x);
	drawGrid(gridInner, bankLen);
	ImGui::PopClipRect();

	// Behavior
	float delta = 0.0;
	if (g.ActiveId == id) {
		delta = g.IO.MouseDelta.x / (inner.Max.x, inner.Max.x);
	}
	return delta;
}

