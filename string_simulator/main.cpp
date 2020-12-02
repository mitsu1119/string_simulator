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

	// プライマリバッファの作成
	DSBUFFERDESC dsbd;
	ZeroMemory(&dsbd, sizeof(DSBUFFERDESC));
	dsbd.dwSize = sizeof(DSBUFFERDESC);
	dsbd.dwFlags = DSBCAPS_PRIMARYBUFFER;
	dsbd.dwBufferBytes = 0;
	hr = pDS->CreateSoundBuffer(&dsbd, &pDSBPrimary, NULL);
	if(hr != DS_OK) return 0;

	// フォーマットの指定
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

	// セカンダリバッファの作成
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
	dsbd2.dwBufferBytes = wfx2.nAvgBytesPerSec;	// 1秒分のセカンダリバッファ
	dsbd2.lpwfxFormat = &wfx2;
	hr = pDS->CreateSoundBuffer(&dsbd2, &pDSBSecondary, NULL);
	if(hr != DS_OK) return 0;

	// イベントのハンドラ
	hNotificationEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(hNotificationEvent == NULL) return 0;

	hNotifyThread = CreateThread(NULL, 0, NotificationProc, hWnd, 0, &dwNotifyThreadID);
	if(hNotifyThread == NULL) return 0;

	dwBufferSize = dsbd2.dwBufferBytes;
	dwBufferUnit = dwBufferSize / BUF_DIVIDES;

	// 長くてもBUF_SEC秒分
	pulses_len = dwBufferSize * BUF_SEC;
	pulses = (short *)malloc(pulses_len * sizeof(short));
	if(pulses == NULL) return 0;
	ZeroMemory(pulses, (pulses_len * sizeof(short)));
	for(size_t i = 0; i < pulses_len; i++) pulses[i] = 5000 * std::sin(0.05 * i);
	
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

// ------------------------------------------------------ 色々便利なやつ --------------------------------------------------

template <typename T>
class Point {
public:
	Point(T x = 0, T y = 0): x(x), y(y) {
	}

	T x, y;
};

// ------------------------------------------------------ 計算用 --------------------------------------------------------------------

// 質点
class MassPoint {
public:
	MassPoint(): z(0), v(0) {
	}

	MassPoint(double z, Point<double> &&coord): z(z), v(0), coord(coord) {
	}

	double z, v;	// 現在の変位、及び速さ
	Point<double> coord;	// ウィンドウ上の座標
};

// 弦
class HString {
private:
	// 弦の分割の精度
	size_t N;

	// 弦の長さと最大振幅
	double length, max_amp;

	// 弦の最初の質点が置かれる座標
	Point<double> pos;

	// バネで近似したときのパラメータ、質量及びバネ係数
	double m, k;

	// 質点たち
	vector<MassPoint> mass;

	// 振幅が最大の質点(中心)のセグメント
	size_t center_segment;

	// 計算用の微小時間
	double dt;

	// 自然ですか？ true: はい false: いいえ
	// 自然ならupdateで次の状態を自然に計算する
	bool is_natural;

	// 波形データ
	vector<double> amps;
	
	// 波形データを記録するフラグ
	bool recording_flag;

	// 各質点の変位からウィンドウ上の座標を求め、その質点を更新する
	void z_to_coord() {
		double dy = this->length / static_cast<double>(this->N);
		for(size_t i = 0; i < this->N + 1; i++) {
			this->mass.at(i).coord.x = this->pos.x + this->mass.at(i).z;
			this->mass.at(i).coord.y = this->pos.y + static_cast<double>(i) * dy;
		}
	}

	// dt秒後の各質点の座標を計算
	void calcNext() {
		// 質点の速さを更新する。かかっている力の大きさから加速度を計算し、それをdt秒分だけ元の質点の速さに加算する
		double f, a;
		for(size_t i = 1; i < this->N; i++) {
			f = -this->k * (this->mass.at(i).z - this->mass.at(i - 1).z) - this->k * (this->mass.at(i).z - this->mass.at(i + 1).z) - this->mass.at(i).v * 0.0001;
			a = f / this->m;
			this->mass.at(i).v += a * this->dt;
		}

		// 質点の変位を更新する。質点の速さを変位に加算する。
		for(size_t i = 1; i < this->N; i++) this->mass.at(i).z += this->mass.at(i).v * this->dt;

		// 質点の座標の更新
		z_to_coord();

		// 波形データの記録
		size_t number = 3;
		if(this->recording_flag) this->amps.emplace_back((short)(1e3 * this->mass.at(number).z));
	}

public:
	HString(Point<double> pos, double length, double max_amp): N(64), pos(pos), length(length), max_amp(max_amp), m(0.10), k(8.3), mass(this->N + 1), center_segment(0), dt(1.0/10.0), is_natural(true), recording_flag(false) {
		z_to_coord();
	}

	// 最大振幅のゲッター
	double get_max_amp() const {
		return this->max_amp;
	}

	// posのゲッター
	const Point<double> &get_pos() const {
		return this->pos;
	}

	// is_naturalのゲッター
	bool get_is_natural() const {
		return is_natural;
	}

	// is_naturalの変更
	void to_natural() {
		this->is_natural = true;
	}

	void to_not_natural() {
		this->is_natural = false;
	}

	// 弦の常態を初期化する。(px, py)に弦が引っ張られている感じになる
	void set_init(double px, double py) {
		// (px, py)が弦を引っ張るような位置になければreturn
		if(py < this->mass.at(1).coord.y || py >= this->length + this->pos.y) {
			to_natural();
			return;
		}

		// pyに対応した弦のセグメントを決定する。i番目の質点のy座標がpyになる
		double dy = static_cast<double>(this->length) / static_cast<double>(this->N);
		size_t i = (py - this->pos.y) / dy;
		px -= this->pos.x;
		double center_z_buf = this->mass.at(i).z;
		this->mass.at(i).z = px;

		// 基準となるセグメントを中心に、上側に固定された質点に向かって弦を張る
		double slope = static_cast<double>(px) / (this->mass.at(i).coord.y - this->pos.y);
		for(size_t j = 1; j < i; j++) this->mass.at(j).z = slope * (this->mass.at(j).coord.y - this->pos.y);

		// 基準となるセグメントを中心に、下側に固定された質点に向かって弦を張る
		slope = -static_cast<double>(px) / (this->length - static_cast<double>(this->mass.at(i).coord.y) + this->pos.y);
		for(size_t j = i + 1; j < this->N; j++) this->mass.at(j).z = px + slope * (this->mass.at(j).coord.y - this->mass.at(i).coord.y);


		// 中心のセグメントを更新
		this->center_segment = i;

		// 弦がすでに非naturalなら、引っ張っている方向と逆向きにカーソルが動いたとき(前の変位と今の変位が異なるとき)、元のx座標を超えたらカーソルにくっつかないようにして音もならないようになる
		if(!this->is_natural && ((center_z_buf >= 0) != (this->mass.at(i).z >= 0)) && center_z_buf != 0) {
			for(auto &m : this->mass) m.z = 0;
			to_natural();
		}

		// 座標の更新
		z_to_coord();

		// 初期状態なので速さを0に初期化
		for(auto &j : this->mass) j.v = 0;
	}

	// 弦は弾かれていますか？ true:はい false:いいえ
	// 弦を弾いていたら、その速さを引数のres_yポインタの中に代入する
	bool is_plucked(const Point<int> &mp, const Point<int> &mp_b, double &res_y) const {
		// 1フレーム前のマウスポインタの座標mp_bと、現在のマウスポインタの座標mpを比較し、弦を跨いでいたら弾いていることになる。
		if((this->pos.x > mp_b.x && this->pos.x <= mp.x) || (this->pos.x < mp_b.x && this->pos.x >= mp.x)) {
			double slope = static_cast<double>(static_cast<long long>(mp.y) - mp_b.y) / static_cast<double>(static_cast<long long>(mp.x) - mp_b.x);
			double intersection_y = static_cast<double>(mp_b.y) + slope * (this->pos.x - static_cast<double>(mp_b.x));

			if(intersection_y < this->mass.at(1).coord.y || intersection_y >= this->length + this->pos.y) return false;

			res_y = intersection_y;
			return true;
		}
		return false;
	}

	// 弦をdt秒後の状態へ移す。ループの回数を上げれば当然1フレームでたくさん状態が進むので周波数が上がったような感じになる
	void update() {
		// 振幅が限界を超えたときnaturalにする
		if(!this->is_natural && this->max_amp < abs(this->mass.at(this->center_segment).z)) {
			this->is_natural = true;
			double v2 = abs(this->mass.at(this->center_segment).z);
			set_init(this->pos.x + this->max_amp * (1 - exp(-v2 / 5.0)), this->mass.at(this->center_segment).coord.y);
			pDSBSecondary->Stop();
			for(size_t i = 0; i < INIT_COUNT; i++) 	ReadWave(pDSBSecondary, dwBufferUnit);
			pDSBSecondary->Play(0, 0, DSBPLAY_LOOPING);
		}

		this->recording_flag = false;
		if(this->is_natural) {
			if(CheckHitKey(KEY_INPUT_ESCAPE) != 0) this->recording_flag = true;
			for(size_t i = 0; i < 1000; i++) calcNext();
			this->recording_flag = false;
		}
	}

	// 弦の描画。各質点と質点の間に線分をかいていく
	void draw() const {
		for(size_t i = 1; i < this->N + 1; i++) {
			DrawLineAA(this->mass.at(i - 1).coord.x, this->mass.at(i - 1).coord.y, this->mass.at(i).coord.x, this->mass.at(i).coord.y, BLACK);
		}
	}

	// filenameのファイルに弦の振幅データを出力
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
};

// ------------------------------------------------------ プログラム全体を管理するやつ -------------------------------------------
class Root {
private:
	HString str;
	bool updateFlag;

	// マウスポインタ。現在の座標と1フレーム前の座標
	Point<int> mp, mp_b;

	void all_pluck() {
		double res_y;
		// ハープは弾かれていますか？
		if(str.get_is_natural() && str.is_plucked(this->mp, this->mp_b, res_y)) {
			str.set_init(this->mp.x, this->mp.y);
			str.to_not_natural();
		} else if(!str.get_is_natural()) {
			str.set_init(this->mp.x, this->mp.y);
		}
	}

public:
	Root(): str(Point<double>(320, 80), 300, 50), updateFlag(false), mp(0, 0), mp_b(0, 0) {
	}

	// メインループ
	void main_loop() {
		// マウスポインタの座標を所得。この時mpを更新する前にmp_bに現在のポインタの座標を退避しておく
		this->mp_b.x = this->mp.x;
		this->mp_b.y = this->mp.y;
		GetMousePoint(&this->mp.x, &this->mp.y);

		// マウスが押されてたらハープを弾く処理をする
		if((GetMouseInput() & MOUSE_INPUT_LEFT) != 0) {
			all_pluck();
			// this->str.set_init(this->mp.x, this->mp.y);
			// this->str.to_not_natural();
		} else {
			if(!this->str.get_is_natural()) {
				this->str.to_natural();
			}
		}

		// ハープの状態の更新
		this->str.update();
	}

	// 画面の更新
	void draw() {
		this->str.draw();
	}

	// 弦の全振幅データを書き出し
	void all_output() const {
		this->str.output("amps.txt");
	}
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	SetOutApplicationLogValidFlag(FALSE);
	ChangeWindowMode(true);
	SetGraphMode(600, 600, 32);
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

	Root root;
	while(ProcessMessage() == 0) {
		ClearDrawScreen();
		root.main_loop();
		root.draw();
		ScreenFlip();
	}

	root.all_output();

	end();
	DxLib_End();
	return 0;
}