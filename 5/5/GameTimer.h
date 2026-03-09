#ifndef GAME_TIMER_HPP
#define GAME_TIMER_HPP

#include <Windows.h>

class GameTimer
{
public:
	GameTimer();

	float DeltaTime() const;
	float TotalTime() const;

	void Reset();
	void Start();
	void Stop();
	void Tick();
private:
	double _secondsPerCount;
	double _deltaTime;

	__int64 _baseTime;

	__int64 _stopTime;
	__int64 _pausedTime;

	__int64 _currTime;
	__int64 _prevTime;

	bool _stopped;
};

#endif // !GAME_TIMER_HPP