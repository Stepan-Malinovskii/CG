#include "GameTimer.h"

GameTimer::GameTimer() : _secondsPerCount(0.0), _prevTime(0), _currTime(0), _baseTime(0),
						 _stopped(false), _deltaTime(-1.0), _stopTime(0), _pausedTime(0)
{
	UINT64 countsPerSec;
	QueryPerformanceFrequency((LARGE_INTEGER*)&countsPerSec);
	_secondsPerCount = 1.0 / (double)countsPerSec;
}

float GameTimer::DeltaTime() const
{
	return (float)_deltaTime;
}

float GameTimer::TotalTime() const
{
	if (_stopped)
	{
		return (float)(((_stopTime - _pausedTime) - _baseTime) * _secondsPerCount);
	}
	else
	{
		return (float)(((_currTime - _pausedTime) - _baseTime) * _secondsPerCount);
	}
}

void GameTimer::Reset()
{
	__int64 currTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&currTime);

	_baseTime = currTime;
	_prevTime = currTime;
	_stopTime = 0;
	_stopped = false;

}

void GameTimer::Start()
{
	if (_stopped)
	{
		__int64 startTime;
		QueryPerformanceCounter((LARGE_INTEGER*)&startTime);

		_pausedTime += (startTime - _stopTime);
		_prevTime = startTime;
		_stopTime = 0;
		_stopped = false;
	}
}

void GameTimer::Stop()
{
	if (!_stopped)
	{
		__int64 currTime;
		QueryPerformanceCounter((LARGE_INTEGER*)&currTime);
		_stopTime = currTime;

		_stopped = true;
	}
}

void GameTimer::Tick()
{
	if (_stopped)
	{
		_deltaTime = 0.0;
		return;
	}

	__int64 currTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&currTime);
	_currTime = currTime;

	_deltaTime = (_currTime - _prevTime) * _secondsPerCount;

	_prevTime = _currTime;

	if (_deltaTime < 0.0)
	{
		_deltaTime = 0.0;
	}
}
