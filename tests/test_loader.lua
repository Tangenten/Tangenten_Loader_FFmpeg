-- SPDX-License-Identifier: LGPL-2.1-or-later

__TLAV_TEST = true

local values = {
	InPlaybackSpeed = 1,
	InReverse = 0,
	InLoopMode = 1,
	InStartBehaviour = 0,
	InStartFrame = 0,
	InEndFrame = 0,
	InStartOffset = 0,
	InInterpolationMode = 0,
}

local function input(name)
	return {
		GetValue = function()
			return { Value = values[name] }
		end,
	}
end

for name in pairs(values) do
	_G[name] = input(name)
end

assert(loadfile("Loader_FFmpeg.fuse"))()

local mapFrame = assert(__TLAV_COMPUTE_FRAME_INDEX)
local req = { Time = 100 }
local info = { frameCount = 10, fps = 25 }
local comp = {
	GlobalStart = 100,
	GetPrefs = function()
		return 25
	end,
}

local function expect(name, frame, blank, nextFrame, interpolation)
	local gotFrame, gotBlank, gotNextFrame, gotInterpolation = mapFrame(req, info, comp)
	assert(gotFrame == frame, name .. ": frame")
	assert(gotBlank == blank, name .. ": blank")
	assert(gotNextFrame == nextFrame, name .. ": next frame")
	assert(math.abs(gotInterpolation - interpolation) < 0.000001, name .. ": interpolation")
end

expect("timeline origin", 0, false, 1, 0)

values.InStartOffset = 3
expect("blank before start", 0, true, 1, 0)

values.InStartBehaviour = 1
expect("hold before start", 0, false, 1, 0)

values.InStartOffset = 0
values.InLoopMode = 2
values.InInterpolationMode = 1
req.Time = 109.5
expect("loop and linear interpolation", 9, false, 0, 0.5)

values.InLoopMode = 3
req.Time = 110.5
expect("ping pong", 7, false, 8, 0.5)

values.InLoopMode = 1
values.InReverse = 1
req.Time = 105
expect("reverse hold", 4, false, 3, 0)

print("Loader playback mapping tests passed")
