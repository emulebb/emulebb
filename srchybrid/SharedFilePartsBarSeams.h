#pragma once

namespace SharedFilePartsBarSeams
{
	struct Colors
	{
		COLORREF crNotShared;
		COLORREF crMissing;
		COLORREF crUnrequested;
		COLORREF crAvailabilityBase;
		COLORREF crAvailabilityHot;
	};

	inline COLORREF InterpolateColor(COLORREF crStart, COLORREF crEnd, UINT uStep, UINT uStepCount)
	{
		if (uStepCount == 0)
			return crStart;
		return RGB(
			GetRValue(crStart) + (GetRValue(crEnd) - GetRValue(crStart)) * static_cast<int>(uStep) / static_cast<int>(uStepCount),
			GetGValue(crStart) + (GetGValue(crEnd) - GetGValue(crStart)) * static_cast<int>(uStep) / static_cast<int>(uStepCount),
			GetBValue(crStart) + (GetBValue(crEnd) - GetBValue(crStart)) * static_cast<int>(uStep) / static_cast<int>(uStepCount));
	}

	inline Colors BuildColors(bool bFlat, bool bLowColorDesktop)
	{
		Colors colors = {};
		if (bLowColorDesktop) {
			colors.crNotShared = RGB(224, 224, 224);
			colors.crMissing = RGB(255, 0, 0);
			colors.crUnrequested = bFlat ? RGB(0, 0, 0) : RGB(104, 104, 104);
			colors.crAvailabilityBase = RGB(0, 210, 255);
			colors.crAvailabilityHot = RGB(0, 0, 255);
			return colors;
		}

		colors.crNotShared = RGB(224, 224, 224);
		colors.crMissing = RGB(255, 0, 0);
		colors.crUnrequested = bFlat ? RGB(0, 0, 0) : RGB(104, 104, 104);
		colors.crAvailabilityBase = RGB(0, 210, 255);
		colors.crAvailabilityHot = RGB(0, 0, 255);

		theApp.LoadSkinColor(_T("SharedPartsBarBackground"), colors.crNotShared);
		theApp.LoadSkinColor(_T("SharedPartsBarMissing"), colors.crMissing);
		theApp.LoadSkinColor(_T("SharedPartsBarUnrequested"), colors.crUnrequested);
		theApp.LoadSkinColor(_T("SharedPartsBarAvailabilityBase"), colors.crAvailabilityBase);
		theApp.LoadSkinColor(_T("SharedPartsBarAvailabilityHot"), colors.crAvailabilityHot);
		return colors;
	}

	inline COLORREF AvailabilityColor(const Colors &colors, UINT uFrequency)
	{
		if (uFrequency == 0)
			return colors.crMissing;
		return InterpolateColor(colors.crAvailabilityBase, colors.crAvailabilityHot, min(uFrequency - 1, 10U), 10U);
	}
}
