#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <opencv2/opencv.hpp>
#include <opencv2/core/directx.hpp>
#include <d3d11.h>
#pragma comment(lib,"d3d11.lib")

#include "parsec-dso.h"

static void logCallback(ParsecLogLevel level, const char* msg, void* opaque)
{
	opaque;

	printf("[%s] %s\n", level == LOG_DEBUG ? "D" : "I", msg);
}

static void guestStateChange(ParsecGuest* guest)
{
	switch (guest->state) {
	case GUEST_CONNECTED:
		printf("%s#%d connected.\n", guest->name, guest->userID);
		break;
	case GUEST_DISCONNECTED:
		printf("%s#%d disconnected.\n", guest->name, guest->userID);
		break;
	default:
		break;
	}
}

int32_t main(int32_t argc, char** argv)
{
	if (argc < 3) {
		printf("Usage: host sessionID camera\n");
		return 1;
	}
	ParsecHostConfig cfg = PARSEC_HOST_DEFAULTS;
	ParsecDSO* parsec = NULL;
	ParsecStatus e = ParsecInit(NULL, NULL, NULL, &parsec);

	cv::VideoCapture camera(atoi(argv[2]));
	camera.set(cv::CAP_PROP_FRAME_WIDTH, 1920);
	camera.set(cv::CAP_PROP_FRAME_HEIGHT, 1080);
	cv::Mat frame;
	camera >> frame;
	cv::Mat as4channelMat(frame.size(), CV_MAKE_TYPE(frame.depth(), 4));

	int conversion[] = { 0, 0, 1, 1, 2, 2, -1, 3 };
	cv::mixChannels(&frame, 1, &as4channelMat, 1, conversion, 4);

	ID3D11Texture2D* tex = 0;
	D3D11_TEXTURE2D_DESC desc = { 0 };
	desc.Width = frame.size().width;
	desc.Height = frame.size().height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	ID3D11Device* device;
	ID3D11DeviceContext* context;
	HRESULT r = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION,
		&device, NULL, &context);
	D3D11_SUBRESOURCE_DATA data;
	data.pSysMem = as4channelMat.ptr();
	data.SysMemPitch = frame.size().width * 4;
	r = device->CreateTexture2D(&desc, &data, &tex);

	ParsecSetLogCallback(parsec, logCallback, NULL);

	
	ParsecHostStart(parsec, HOST_GAME, &cfg, argv[1]);

	while (true) {
		camera >> frame;

		int conversion[] = { 0, 0, 1, 1, 2, 2, -1, 3 };
		cv::mixChannels(&frame, 1, &as4channelMat, 1, conversion, 4);
		r = device->CreateTexture2D(&desc, &data, &tex);
		ParsecStatus e = ParsecHostD3D11SubmitFrame(parsec, device, context, tex);
		tex->Release();
		for (ParsecHostEvent event; ParsecHostPollEvents(parsec, 1, &event);) {
			switch (event.type) {
			case HOST_EVENT_GUEST_STATE_CHANGE:
				guestStateChange(&event.guestStateChange.guest);
				break;
			default:
				break;
			}
		}
	}

except:
	printf("%d\n", e);
	ParsecDestroy(parsec);

	return 0;
}
