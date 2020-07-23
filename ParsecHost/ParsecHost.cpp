#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <opencv2/opencv.hpp>
#include <opencv2/core/directx.hpp>
#include <d3d11.h>
#include <opencv2/imgproc/types_c.h>
#include <iostream> 
#include <thread>
#include <mutex>
#include <queue>
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

void createDevice(ID3D11Device *&d3dDevice, ID3D11DeviceContext *&d3dContext) {
	UINT createDeviceFlags = 0;

#ifdef _DEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_FEATURE_LEVEL featureLevel;
	HRESULT  hr = D3D11CreateDevice(
		0,
		D3D_DRIVER_TYPE_HARDWARE,
		0,
		createDeviceFlags,
		0,
		0,
		D3D11_SDK_VERSION,
		&d3dDevice,
		&featureLevel,
		&d3dContext);

	if (FAILED(hr))
	{
		OutputDebugStringA("D3D11CreateDevice Failed.");
	}
	if (featureLevel != D3D_FEATURE_LEVEL_11_0)
	{
		OutputDebugStringA("Direct3D FeatureLevel 11 unsupported.");
	}
}

int32_t test_camera(cv::VideoCapture *vc_camera)
{
	cv::Mat frame;
	cv::namedWindow("Output Window");
	for (;;) {
		if (!vc_camera->read(frame)) {
			std::cout << "No frame" << std::endl;
			cv::waitKey(1);
		}
		cv::imshow("Output Window", frame);
		if (cv::waitKey(1) >= 0) break;
	}
	return 0;
}

int32_t init_camera(cv::VideoCapture *vc_camera, int camera_id)
{
	//std::string camera_url = "rtmp://192.168.1.188:1950/live/origin" + std::to_string(camera_id);
	std::string camera_url = "rtmp://192.168.1.188/live/live";
	*vc_camera = cv::VideoCapture(camera_url);
	vc_camera->set(cv::CAP_PROP_FRAME_WIDTH, 1440);
	vc_camera->set(cv::CAP_PROP_FRAME_HEIGHT, 1440);
	vc_camera->set(cv::CAP_PROP_BUFFERSIZE, 0);

	//test_camera(vc_camera);

	if (vc_camera->isOpened()) {
		return 0;
	}
	else {
		return -1;
	}
}

void read_thread(cv::VideoCapture *vc_camera, std::list<cv::Mat> *frame_que)
{
	cv::Mat frame;
	while (true) {

		vc_camera->read(frame);
		if (frame_que->empty() == false) {
			frame_que->push_front(frame);
			/*frame_que->pop_back();*/
		}
		else {
			frame_que->push_front(frame);
		}
	}
}

std::thread start_camera_thread(cv::VideoCapture *vc_camera, int camera_id, std::list<cv::Mat> *frame_que)
{
	int ret_val;
	ret_val = init_camera(vc_camera, camera_id);
	std::thread read(read_thread, vc_camera, frame_que);
	return read;
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

	cv::VideoCapture camera;
	cv::Mat frame;
	std::list<cv::Mat> queue_1;

	std::thread cam_thread = start_camera_thread(&camera, 1, &queue_1);

	while (queue_1.empty()) {

	}
	frame = queue_1.front();
	/*camera.read(frame);*/
	cv::Mat as4channelMat(frame.size(), CV_MAKE_TYPE(frame.depth(), 4));
	cv::Mat convertedImage(frame.size(), CV_MAKE_TYPE(frame.depth(), 4));

	int conversion[] = { 0, 0, 1, 1, 2, 2, -1, 3 };
	cv::mixChannels(&frame, 1, &as4channelMat, 1, conversion, 4);

	try {
		cv::cvtColor(frame, convertedImage, CV_BGR2BGRA); //CV_8UC3 -> CV_8UC4
	}
	catch (...) {}


	ID3D11Device* device = nullptr;
	ID3D11DeviceContext* context = nullptr;
	createDevice(device, context);

	if (cv::ocl::haveOpenCL()) {
		cv::ocl::Context oclContext = cv::directx::ocl::initializeContextFromD3D11Device(device);
	}

	D3D11_TEXTURE2D_DESC desc;
	desc.Width = frame.size().width;
	desc.Height = frame.size().height;
	desc.MipLevels = desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ;
	desc.MiscFlags = 0;

	ID3D11Texture2D* tex = nullptr;

	HRESULT r = device->CreateTexture2D(&desc, nullptr, &tex);

	try {
		cv::directx::convertToD3D11Texture2D(convertedImage, tex);
	}
	catch (cv::Exception& e)
	{
		std::cerr << "ERROR: " << e.msg << std::endl;
		throw e;
	}
	ParsecSetLogCallback(parsec, logCallback, NULL);

	
//	ParsecHostStart(parsec, HOST_GAME, &cfg, argv[1]);
	ParsecHostStart(parsec, HOST_GAME, &cfg, "4afcda931776617df118fab164e0a94c9be6f931fb4e89f8702f16a0c453dfa2");

	//test_camera(&camera);

	while (true) {
			frame = queue_1.front();
			try {
				cv::cvtColor(frame, convertedImage, CV_BGR2BGRA); //CV_8UC3 -> CV_8UC4
				cv::directx::convertToD3D11Texture2D(convertedImage, tex);
			}
			catch(...){}
			ParsecStatus e = ParsecHostD3D11SubmitFrame(parsec, device, context, tex);

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
