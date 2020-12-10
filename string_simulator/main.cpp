#include "DxLib.h"
#include <vector>
#include <string>
#include <fstream>
#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>

#pragma comment(lib, "dsound.lib")
#pragma comment(lib, "dxguid.lib")

using namespace std;

#define BLACK GetColor(0, 0, 0)
#define SAMPLING_FREQ 44100
#define BUF_SEC 1
#define BUF_DIVIDES 100
#define INIT_COUNT 5

#define RELEASE(x) if(x){x->Release(); x = NULL;}

constexpr double max_amp = 902448;
constexpr double amp_adj = 16380.0 / max_amp;

// ------------------------------------------------------ 音関係 -----------------------------------------------

LPDIRECTSOUND8 pDS = NULL;
LPDIRECTSOUNDBUFFER pDSBPrimary = NULL;
LPDIRECTSOUNDBUFFER pDSBSecondary = NULL;
LPDIRECTSOUNDNOTIFY pDSNotify;
DSBPOSITIONNOTIFY aPosNotify[BUF_DIVIDES];

HANDLE hNotificationEvent = NULL;
DWORD dwNotifyThreadID = 0;
DWORD dwBufferSize = 0;
DWORD dwBufferUnit = 0;
HANDLE hNotifyThread = NULL;

short *pulses;
size_t pulses_len;
int read_pos = 0;

DWORD ReadWave(LPDIRECTSOUNDBUFFER pDSBuffer, DWORD dwSize) {
	HANDLE hFile;
	VOID *lpBuffer;
	DWORD buffersize;
	DWORD filesize;

	if(read_pos == BUF_DIVIDES * BUF_SEC) {
		read_pos++;
		return 1;
	}

	if(read_pos > BUF_DIVIDES * BUF_SEC) {
		return 0;
	}

	pDSBuffer->Lock((read_pos % BUF_DIVIDES) * dwSize, dwSize, &lpBuffer, &buffersize, NULL, NULL, 0);

	memcpy((char *)lpBuffer, (char *)pulses + dwSize * read_pos, dwSize);

	pDSBuffer->Unlock(lpBuffer, buffersize, NULL, 0);
	read_pos++;

	return 1;
}

// スレッド処理
DWORD WINAPI NotificationProc(LPVOID lpParameter) {
	MSG msg;
	HWND hWnd = (HWND)lpParameter;
	BOOL bDone = FALSE;
	DWORD dwResult;

	while(!bDone) {
		dwResult = MsgWaitForMultipleObjects(1, &hNotificationEvent, FALSE, INFINITE, QS_ALLEVENTS);
		switch(dwResult) {
		case WAIT_OBJECT_0 + 0:
			dwResult = ReadWave(pDSBSecondary, dwBufferUnit);
			if(dwResult == 0) {
				read_pos = 0;
				pDSBSecondary->Stop();
			}
			break;
		case WAIT_OBJECT_0 + 1:
			while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
				if(msg.message == WM_QUIT) {
					bDone = TRUE;
				}
			}
			break;
		}
	}
	read_pos = 0;
	pDSBSecondary->Stop();
	return 0;
}

int initDs(HWND hWnd) {
	HRESULT hr;

	hr = DirectSoundCreate8(NULL, &pDS, NULL);
	if(hr != DS_OK) return 0;

	hr = pDS->SetCooperativeLevel(hWnd, DSSCL_PRIORITY);
	if(hr != DS_OK) return 0;

	DSBUFFERDESC dsbd;
	ZeroMemory(&dsbd, sizeof(DSBUFFERDESC));
	dsbd.dwSize = sizeof(DSBUFFERDESC);
	dsbd.dwFlags = DSBCAPS_PRIMARYBUFFER;
	dsbd.dwBufferBytes = 0;
	hr = pDS->CreateSoundBuffer(&dsbd, &pDSBPrimary, NULL);
	if(hr != DS_OK) return 0;

	WAVEFORMATEX wfx;
	ZeroMemory(&wfx, sizeof(WAVEFORMATEX));
	wfx.wFormatTag = (WORD)WAVE_FORMAT_PCM;
	wfx.nChannels = (WORD)1;
	wfx.nSamplesPerSec = (DWORD)SAMPLING_FREQ;
	wfx.wBitsPerSample = (WORD)16;
	wfx.nBlockAlign = (WORD)(wfx.wBitsPerSample / 8 * wfx.nChannels);
	wfx.nAvgBytesPerSec = (DWORD)(wfx.nSamplesPerSec * wfx.nBlockAlign);
	hr = pDSBPrimary->SetFormat(&wfx);
	if(hr != DS_OK) return 0;

	WAVEFORMATEX wfx2;
	ZeroMemory(&wfx2, sizeof(WAVEFORMATEX));
	wfx2.wFormatTag = (WORD)WAVE_FORMAT_PCM;
	wfx2.nChannels = 1;
	wfx2.nSamplesPerSec = SAMPLING_FREQ;
	wfx2.wBitsPerSample = 16;
	wfx2.nBlockAlign = (WORD)(wfx2.wBitsPerSample / 8 * wfx2.nChannels);
	wfx2.nAvgBytesPerSec = (DWORD)(wfx2.nSamplesPerSec * wfx2.nBlockAlign);

	DSBUFFERDESC dsbd2;
	ZeroMemory(&dsbd2, sizeof(DSBUFFERDESC));
	dsbd2.dwSize = sizeof(DSBUFFERDESC);
	dsbd2.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_GLOBALFOCUS | DSBCAPS_STATIC | DSBCAPS_LOCDEFER;
	dsbd2.dwBufferBytes = wfx2.nAvgBytesPerSec;
	dsbd2.lpwfxFormat = &wfx2;
	hr = pDS->CreateSoundBuffer(&dsbd2, &pDSBSecondary, NULL);
	if(hr != DS_OK) return 0;

	hNotificationEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(hNotificationEvent == NULL) return 0;

	hNotifyThread = CreateThread(NULL, 0, NotificationProc, hWnd, 0, &dwNotifyThreadID);
	if(hNotifyThread == NULL) return 0;

	dwBufferSize = dsbd2.dwBufferBytes;
	dwBufferUnit = dwBufferSize / BUF_DIVIDES;

	pulses_len = dwBufferSize * BUF_SEC;
	pulses = (short *)malloc(pulses_len * sizeof(short));
	if(pulses == NULL) return 0;
	ZeroMemory(pulses, (pulses_len * sizeof(short)));
	
	for(size_t i = 0; i < BUF_DIVIDES; i++) {
		aPosNotify[i].dwOffset = dwBufferUnit * (i + 1) - 1;
		aPosNotify[i].hEventNotify = hNotificationEvent;
	}

	hr = pDSBSecondary->QueryInterface(IID_IDirectSoundNotify, (VOID **)&pDSNotify);
	if(hr != DS_OK) return 0;

	hr = pDSNotify->SetNotificationPositions(BUF_DIVIDES, aPosNotify);
	if(hr != DS_OK) return 0;

	return 1;
}
void end() {
	pDSBSecondary->Stop();

	CloseHandle(hNotificationEvent);
	CloseHandle(hNotifyThread);
	free(pulses);
	RELEASE(pDSNotify);
	RELEASE(pDSBSecondary);
	RELEASE(pDSBPrimary);
	RELEASE(pDS);
}

// ------------------------------------------------------ 便利なやつ --------------------------------------------------

template <typename T>
class Point {
public:
	Point(T x = 0, T y = 0): x(x), y(y) {
	}

	T x, y;
};

class Image {
private:
	int handle;
	double exRate, angle;
	Point<int> coord;

public:
	Image(const TCHAR *path, int x, int y, double exRate, double angle): coord(x, y), exRate(exRate), angle(angle) {
		SetTransColor(255, 255, 255);
		this->handle = LoadGraph(path);
	}

	void draw() const {
		DrawRotaGraph(this->coord.x, this->coord.y, this->exRate, this->angle, handle, true);
	}
};

// ------------------------------------------------------ 弦関係 --------------------------------------------------------------------

class MassPoint {
public:
	MassPoint(): z(0), v(0) {
	}

	MassPoint(double z, Point<double> &&coord): z(z), v(0), coord(coord) {
	}

	double z, v;
	Point<double> coord;
};

class HString {
private:
	size_t N;
	double length, max_amp;
	Point<double> pos;

	double m, k;
	vector<MassPoint> mass;
	size_t center_segment;

	double dt;

	bool is_natural;
	vector<double> amps;
	bool recording_flag;
	size_t now_pulses_cnt;

	void z_to_coord() {
		double dy = this->length / static_cast<double>(this->N);
		for(size_t i = 0; i < this->N + 1; i++) {
			this->mass.at(i).coord.x = this->pos.x + this->mass.at(i).z;
			this->mass.at(i).coord.y = this->pos.y + static_cast<double>(i) * dy;
		}
	}

	void calcNext() {
		double f, a;
		for(size_t i = 1; i < this->N; i++) {
			f = -this->k * (this->mass.at(i).z - this->mass.at(i - 1).z) - this->k * (this->mass.at(i).z - this->mass.at(i + 1).z) - this->mass.at(i).v * 0.0001;
			a = f / this->m;
			this->mass.at(i).v += a * this->dt;
		}

		for(size_t i = 1; i < this->N; i++) this->mass.at(i).z += this->mass.at(i).v * this->dt;

		z_to_coord();

		size_t number = 3;
		if(this->recording_flag) this->amps.emplace_back((short)(1e3 * this->mass.at(number).z));
		if(this->recording_flag) {
			pulses[this->now_pulses_cnt] = (short)(1e3 * this->mass.at(number).z);
			this->now_pulses_cnt++;
		}
	}

public:
	HString(Point<double> pos, double length, double max_amp): N(64), pos(pos), length(length), max_amp(max_amp), m(0.10), k(8.3), mass(this->N + 1), center_segment(0), dt(1.0/10.0), is_natural(true), recording_flag(false), now_pulses_cnt(0) {
		z_to_coord();
	}

	double get_max_amp() const {
		return this->max_amp;
	}

	const Point<double> &get_pos() const {
		return this->pos;
	}

	bool get_is_natural() const {
		return is_natural;
	}

	void to_natural() {
		this->is_natural = true;
	}

	void to_not_natural() {
		this->is_natural = false;
	}

	void set_init(double px, double py) {
		if(py < this->mass.at(1).coord.y || py >= this->length + this->pos.y) {
			to_natural();
			return;
		}

		double dy = static_cast<double>(this->length) / static_cast<double>(this->N);
		size_t i = (py - this->pos.y) / dy;
		px -= this->pos.x;
		double center_z_buf = this->mass.at(i).z;
		this->mass.at(i).z = px;

		double slope = static_cast<double>(px) / (this->mass.at(i).coord.y - this->pos.y);
		for(size_t j = 1; j < i; j++) this->mass.at(j).z = slope * (this->mass.at(j).coord.y - this->pos.y);

		slope = -static_cast<double>(px) / (this->length - static_cast<double>(this->mass.at(i).coord.y) + this->pos.y);
		for(size_t j = i + 1; j < this->N; j++) this->mass.at(j).z = px + slope * (this->mass.at(j).coord.y - this->mass.at(i).coord.y);


		this->center_segment = i;

		if(!this->is_natural && ((center_z_buf >= 0) != (this->mass.at(i).z >= 0)) && center_z_buf != 0) {
			for(auto &m : this->mass) m.z = 0;
			to_natural();
		}

		z_to_coord();

		for(auto &j : this->mass) j.v = 0;
	}

	bool is_plucked(const Point<int> &mp, const Point<int> &mp_b, double &res_y) const {
		if((this->pos.x > mp_b.x && this->pos.x <= mp.x) || (this->pos.x < mp_b.x && this->pos.x >= mp.x)) {
			double slope = static_cast<double>(static_cast<long long>(mp.y) - mp_b.y) / static_cast<double>(static_cast<long long>(mp.x) - mp_b.x);
			double intersection_y = static_cast<double>(mp_b.y) + slope * (this->pos.x - static_cast<double>(mp_b.x));

			if(intersection_y < this->mass.at(1).coord.y || intersection_y >= this->length + this->pos.y) return false;

			res_y = intersection_y;
			return true;
		}
		return false;
	}

	void update() {
		if(!this->is_natural && this->max_amp < abs(this->mass.at(this->center_segment).z)) {
			this->is_natural = true;
			double v2 = abs(this->mass.at(this->center_segment).z);
			set_init(this->pos.x + this->max_amp * (1 - exp(-v2 / 5.0)), this->mass.at(this->center_segment).coord.y);
			play();
		}

		if(this->is_natural) {
			if(CheckHitKey(KEY_INPUT_ESCAPE) != 0) this->recording_flag = true;
			for(size_t i = 0; i < 1000; i++) calcNext();
			// this->recording_flag = false;
		}
	}

	void draw() const {
		for(size_t i = 1; i < this->N + 1; i++) {
			DrawLineAA(this->mass.at(i - 1).coord.x, this->mass.at(i - 1).coord.y, this->mass.at(i).coord.x, this->mass.at(i).coord.y, BLACK);
		}
	}

	void output(string filename) const {
		ofstream of(filename);
		of << "[";
		if(this->amps.size() > 0) {
			for(size_t i = 0; i + 1 < this->amps.size(); i++) of << this->amps.at(i) << ", ";
			of << this->amps.back(); //  amp_adj;
		}
		of << "]";
		of.close();
	}

	void play() {
		pDSBSecondary->Stop();
		this->recording_flag = true;
		this->now_pulses_cnt = 0;
		for(size_t i = 0; i < dwBufferUnit * INIT_COUNT / 2; i++) calcNext();
		for(size_t i = 0; i < INIT_COUNT; i++) 	ReadWave(pDSBSecondary, dwBufferUnit);
		pDSBSecondary->Play(0, 0, DSBPLAY_LOOPING);
	}
};

// ------------------------------------------------------ 管理クラス -------------------------------------------
class Root {
private:
	HString str;
	bool updateFlag;

	Point<int> mp, mp_b;

	Image harp_img;

	void all_pluck() {
		double res_y;
		if(str.get_is_natural() && str.is_plucked(this->mp, this->mp_b, res_y)) {
			str.set_init(this->mp.x, this->mp.y);
			str.to_not_natural();
		} else if(!str.get_is_natural()) {
			str.set_init(this->mp.x, this->mp.y);
		}
	}

public:
	Root(Image harp): str(Point<double>(320, 80), 300, 50), updateFlag(false), mp(0, 0), mp_b(0, 0), harp_img(harp) {
	}

	void main_loop() {
		this->mp_b.x = this->mp.x;
		this->mp_b.y = this->mp.y;
		GetMousePoint(&this->mp.x, &this->mp.y);

		if((GetMouseInput() & MOUSE_INPUT_LEFT) != 0) {
			all_pluck();
			// this->str.set_init(this->mp.x, this->mp.y);
			// this->str.to_not_natural();
		} else {
			if(!this->str.get_is_natural()) {
				this->str.to_natural();
				this->str.play();
			}
		}

		this->str.update();
	}

	void draw() {
		this->harp_img.draw();
		this->str.draw();
	}

	void all_output() const {
		this->str.output("amps.txt");
	}
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	SetOutApplicationLogValidFlag(FALSE);
	ChangeWindowMode(false);
	SetGraphMode(1600, 900, 32);
	SetBackgroundColor(255, 255, 255);
	SetMainWindowText(_T("Jikken"));
	if(DxLib_Init() == -1) return -1;

	SetDrawScreen(DX_SCREEN_BACK);

	HWND hWnd = GetMainWindowHandle();

	int result = initDs(hWnd);
	if(result == 0) {
		end();
		DxLib_End();
		return -1;
	}

	Root root(Image(_T("harp.png"), 800, 450, 0.8, 0));
	while(ProcessMessage() == 0) {
		ClearDrawScreen();
		root.main_loop();
		root.draw();
		ScreenFlip();
	}

	// root.all_output();

	end();
	DxLib_End();
	return 0;
}