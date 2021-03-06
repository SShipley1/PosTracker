#include <opencv2/opencv.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/features2d/features2d.hpp>
#include <opencv2/tracking.hpp>
#include "PosTracker.h"
#include "PosTrackerEditor.h"
#include "Camera.h"
#include "/home/robin/Dropbox/Programming/c/utils/opencvmagic.h"

#include <array>
#include <vector>

cv::Mat mask;
cv::Mat displayMask;
cv::Mat pathFrame;
cv::Mat debug_frame;
// values for excluding points outside the mask for use in PosTS class below
int left_mat_edge;
int right_mat_edge;
int top_mat_edge;
int bottom_mat_edge;

// originally nabbed this from:
// https://www.gnu.org/software/libc/manual/html_node/Elapsed-Time.html
// then adpated to work with timespec struct as third arg (*y)
int timevalspec_subtract (struct timeval *result, struct timeval *x, struct timeval *y)
{
  /* Perform the carry for the later subtraction by updating y. */
  if (x->tv_usec < y->tv_usec) {
    int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
    y->tv_usec -= 1000000 * nsec;
    y->tv_sec += nsec;
  }
  if (x->tv_usec - y->tv_usec > 1000000) {
    int nsec = (x->tv_usec - y->tv_usec) / 1000000;
    y->tv_usec += 1000000 * nsec;
    y->tv_sec -= nsec;
  }

  /* Compute the time remaining to wait.
     tv_usec is certainly positive. */
  result->tv_sec = x->tv_sec - y->tv_sec;
  result->tv_usec = x->tv_usec - y->tv_usec;

  /* Return 1 if result is negative. */
  return x->tv_sec < y->tv_sec;
};
int
timeval_subtract (struct timeval *result, struct timeval *x, struct timeval *y)
{
  /* Perform the carry for the later subtraction by updating y. */
  if (x->tv_usec < y->tv_usec) {
    int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
    y->tv_usec -= 1000000 * nsec;
    y->tv_sec += nsec;
  }
  if (x->tv_usec - y->tv_usec > 1000000) {
    int nsec = (x->tv_usec - y->tv_usec) / 1000000;
    y->tv_usec += 1000000 * nsec;
    y->tv_sec -= nsec;
  }

  /* Compute the time remaining to wait.
     tv_usec is certainly positive. */
  result->tv_sec = x->tv_sec - y->tv_sec;
  result->tv_usec = x->tv_usec - y->tv_usec;

  /* Return 1 if result is negative. */
  return x->tv_sec < y->tv_sec;
};


// void drawRect()
bool drawing = false;
bool roi_done = false;
bool tracker_init = false;
int initx = -1;
int inity = -1;

class PosTS
{
public:
	PosTS() {};
	PosTS(struct timeval tv, cv::Mat & src) : m_tv(tv), m_src(src) {};
	~PosTS() {};
	cv::Mat simpleColorDetect(cv::Mat & frame)
	{
		if ( ! frame.empty() ) {
			cv::extractChannel(frame, red_channel, 2);
			cv::Mat roi = red_channel(roi_rect);
			auto roi_mask = mask(roi_rect);
			// cv::GaussianBlur(roi, roi, cv::Size(7,7), 3.0, 3.0);
			cv::threshold(roi, roi, 200, 1000, cv::THRESH_BINARY);
			// cv::erode(roi, roi, kern, cv::Point(-1,-1), 1);
			cv::Mat labels, stats, centroids;
			int nlabels = cv::connectedComponentsWithStats(roi, labels, stats, centroids, 8, CV_32S, cv::CCL_WU);
			cv::Size stats_size = stats.size();
			if ( stats_size.height > 1 ) {
				int maxpix = 0;
				int biggestComponent = 0;
				for (int i = 1; i < stats_size.height; ++i)
				{
					if ( stats.at<int>(i, cv::CC_STAT_AREA) > maxpix ) {
						maxpix = stats.at<int>(i, cv::CC_STAT_AREA);
						biggestComponent = i;
					}
				}
				double x_centroid, y_centroid;
				x_centroid = centroids.at<double>(biggestComponent, 0);
				y_centroid = centroids.at<double>(biggestComponent, 1);
				maxloc.x = static_cast<int>(x_centroid) + left_mat_edge;
				maxloc.y = static_cast<int>(y_centroid) + top_mat_edge;
			}
			else {
				cv::minMaxLoc(roi, NULL, NULL, NULL, &maxloc, ~roi_mask);
				maxloc.x = maxloc.x + left_mat_edge;
				maxloc.y = maxloc.y + top_mat_edge;
			}
			m_xy[0] = (juce::uint32)maxloc.x;
			m_xy[1] = (juce::uint32)maxloc.y;

			return roi;
		}
	}
	cv::Mat processFrame(cv::Mat & frame)
	{
		cv::Mat cpy;
		frame.copyTo(cpy);
		cv::cvtColor(cpy, cpy, cv::COLOR_BGR2GRAY);
		cv::GaussianBlur(cpy, cpy, cv::Size(3,3), 3.0, 2.0);
		cv::threshold(cpy, cpy, 180, 1000, cv::THRESH_BINARY);
		cv::Mat kern = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3,3));
		cv::erode(cpy, cpy, kern, cv::Point(-1,-1), 1);
		return cpy;
	}

	void blobDetect(std::vector<cv::Point2f> & centre, std::vector<float> & radius)
	{
		cv::Mat m_blah = processFrame(m_src);
		std::vector<std::vector<cv::Point>> cnts;
		std::vector<cv::Vec4i> hierarchy;
		cv::findContours(m_blah, cnts, hierarchy, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
		cv::Point2f __centre;
		float __radius;
		float old_radius = 0;
		int j = 0;
		for (int i = 0; i < cnts.size(); ++i)
		{
			cv::minEnclosingCircle(cnts[i], __centre, __radius);
			centre.push_back(__centre);
			radius.push_back(__radius);
			m_xy[0] = (juce::uint32)__centre.x;
			m_xy[1] = (juce::uint32)__centre.y;
		}
	}

	struct timeval getTimeVal() { return m_tv; }
	juce::uint32 * getPos()
	{
		return m_xy;
	}
	friend std::ostream & operator<<(std::ostream & out, const PosTS & p)
	{
		out << "\t" << p.m_xy[0] << "\t" << p.m_xy[1] << std::endl;
		return out;
	}
	cv::Mat kern = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3,3));
	cv::Mat red_channel, m_src;
	cv::Point maxloc;
	juce::uint32 m_xy[2];
	struct timeval m_tv;
	std::vector<cv::KeyPoint> kps;
	cv::Rect roi_rect = cv::Rect(left_mat_edge, top_mat_edge, right_mat_edge-left_mat_edge, bottom_mat_edge-top_mat_edge);
};

PosTracker::PosTracker() : GenericProcessor("Pos Tracker"), Thread("PosTrackerThread")
{
	setProcessorType (PROCESSOR_TYPE_SOURCE);
	sendSampleCount = true; // only sending events
}

PosTracker::~PosTracker()
{
	if ( currentCam->ready() )
	{
		currentCam->stop_device();
		currentCam->uninit_device();
		currentCam->close_device();
	}
}

	void PosTracker::sendTimeStampedPosToMidiBuffer(std::shared_ptr<PosTS> p)
{
	xy = p->getPos();
	tv = p->getTimeVal();
	xy_ts[0] = xy[0];
	xy_ts[1] = xy[1];
	clock_gettime(CLOCK_MONOTONIC, &ts);
	double nowTime = ts.tv_sec + ((double)ts.tv_nsec / 1e9);
	double frameTime = tv.tv_sec + ((double)tv.tv_usec / 1e6);
	double time_delta = nowTime - frameTime;
	xy_ts[2] = juce::uint32(time_delta * 1e6);
	BinaryEventPtr event = BinaryEvent::createBinaryEvent(messageChannel, CoreServices::getGlobalTimestamp(), xy_ts, sizeof(juce::uint32)*3);
	addEvent(messageChannel, event, 0);
}

void PosTracker::process(AudioSampleBuffer& buffer)
{
		setTimestampAndSamples(CoreServices::getGlobalTimestamp(), 1);
		lock.enter();
		while ( ! posBuffer.empty() )
		{
			std::shared_ptr<PosTS> p = std::move(posBuffer.front());
			sendTimeStampedPosToMidiBuffer(std::move(p));
			posBuffer.pop();
		}
		lock.exit();
}

void PosTracker::createEventChannels()
{
	std::cout << "createEventChannels called" << std::endl;
	EventChannel * chan = new EventChannel(EventChannel::UINT32_ARRAY, 1, 3, CoreServices::getGlobalSampleRate(), this);
	chan->setName("Position");
	chan->setDescription("x-y position of animal");
	chan->setIdentifier("external.position.rawData");
	eventChannelArray.add(chan);
	messageChannel = chan;
}

void PosTracker::startRecording()
{
	openCamera();
	startStreaming();
}

int PosTracker::getControlValues(__u32 id, __s32 & min, __s32 & max, __s32 & step)
{
	if ( camReady )
		return currentCam->get_control_values(id, min, max, step);
	else
		return 1;
}

void PosTracker::changeExposureTo(int autoOrManual)
{
	if ( camReady )
		currentCam->switch_exposure_type(autoOrManual);
}
void PosTracker::stopRecording()
{
	stopStreaming();
}

void PosTracker::openCamera()
{
	if ( ! currentCam->ready() )
		currentCam->open_device();
	if ( ! currentCam->initialized() )
		currentCam->init_device();
	if ( ! currentCam->started() )
		currentCam->start_device();
	camReady = true;
}

void PosTracker::stopCamera()
{
	currentCam->stop_device();
	currentCam->uninit_device();
	currentCam->close_device();
}

void PosTracker::startStreaming()
{
	if ( camReady )
	{
		if ( liveStream == true )
		{
			cv::namedWindow("Live stream", cv::WINDOW_NORMAL & cv::WND_PROP_ASPECT_RATIO);
		}
		threadRunning = true;
		posBuffer = std::queue<std::shared_ptr<PosTS>>{}; // clear the buffer
		startThread();
	}
}

void PosTracker::stopStreaming()
{
	if ( threadRunning )
	{
		threadRunning = false;
		posBuffer = std::queue<std::shared_ptr<PosTS>>{};
		stopThread(10000);
		stopCamera();
	}
}

void PosTracker::showLiveStream(bool val)
{
	liveStream = val;
	if ( liveStream == false ){
		cv::destroyAllWindows();
	}
}

void PosTracker::run()
{
	cv::Mat frame,  roi;
	struct timeval tv;
	std::vector<cv::Point2d> pts{2};
	unsigned int count = 0;
	if ( ! pathFrame.empty() )
		pathFrame = cv::Scalar(0);

	auto ed = static_cast<PosTrackerEditor*>(getEditor());

	while ( threadRunning )
	{
		if ( camReady )
		{
			juce::int64 st = cv::getTickCount();
			currentCam->read_frame(frame, tv);

			if ( !frame.empty() )
			{
				lock.enter();
				pos_tracker = std::make_shared<PosTS>(tv, frame);
				roi = pos_tracker->simpleColorDetect(frame);
				// pos_tracker->detect();

				if ( liveStream == true )
				{
					// auto pb = posBuffer.front();
					auto xy = pos_tracker->getPos();
					pts[count%2] = cv::Point2d(double(xy[0]), double(xy[1]));
					ed->setInfoValue(InfoLabelType::XPOS, (double)xy[0]);
					ed->setInfoValue(InfoLabelType::YPOS, (double)xy[1]);

					cv::bitwise_and(frame, displayMask, frame, mask);
					if ( pts[1].x > 0 && pts[1].y > 0  && path_overlay == true )
					{
						cv::line(pathFrame, pts[0], pts[1], cv::Scalar(0,255,0), 2, cv::LINE_8);
						cv::addWeighted(frame, 1.0, pathFrame, 0.5, 0.0, frame);
					}
					cv::rectangle(frame, cv::Point(double(xy[0])-3, double(xy[1])-3), cv::Point(double(xy[0])+3, double(xy[1])+3), cv::Scalar(0,255,0), -1,1);
					cv::imshow("Live stream", frame);
					cv::waitKey(1);

					double fps = cv::getTickFrequency() / (cv::getTickCount() - st);
					ed->setInfoValue(InfoLabelType::FPS, fps);
					++count;
				}
				posBuffer.push(pos_tracker);
				lock.exit();
			}
		}
	}
	threadRunning = false;
	std::cout << std::endl;
}

void PosTracker::adjustBrightness(int val)
{
	if ( currentCam )
		currentCam->set_control_value(V4L2_CID_BRIGHTNESS, val);
	brightness = val;
}

void PosTracker::adjustContrast(int val)
{
	if ( currentCam )
		currentCam->set_control_value(V4L2_CID_CONTRAST, val);
	contrast = val;
}

void PosTracker::adjustExposure(int val)
{
	if ( currentCam )
		currentCam->set_control_value(V4L2_CID_EXPOSURE_ABSOLUTE, val);
	exposure = val;
}

void PosTracker::adjustVideoMask(BORDER edge, int val)
{
	switch (edge)
	{
		case BORDER::LEFT: left_border = val; break;
		case BORDER::RIGHT: right_border = val; break;
		case BORDER::TOP: top_border = val; break; // co-ords switched
		case BORDER::BOTTOM: bottom_border = val; break;
	}
}

void PosTracker::adjustTrackerMask(int left, int top, int right, int bottom) {
	if ( pos_tracker ) {
		cv::Rect r(cv::Point(left, top), cv::Point(right, bottom));
	}
}

void PosTracker::makeVideoMask()
{
	if ( currentCam )
	{
		if ( currentCam->get_current_format() )
		{
			lock.enter();
			std::pair<int, int> res = getResolution();
			// update the "locally global" values of the edges
			left_mat_edge = left_border;
			right_mat_edge = right_border;
			top_mat_edge = top_border;
			bottom_mat_edge = bottom_border;
			// do the masking
			mask = cv::Mat::ones(res.second, res.first, CV_8UC1);
			cv::rectangle(mask, cv::Point(left_border, top_border), cv::Point(right_border, bottom_border), cv::Scalar(0), -1, 8, 0);
			mask.copyTo(displayMask);
			mask.copyTo(pathFrame);
			cv::cvtColor(displayMask, displayMask, cv::COLOR_GRAY2BGR);
			cv::cvtColor(pathFrame, pathFrame, cv::COLOR_GRAY2BGR);
			lock.exit();
		}
	}
}

int PosTracker::getVideoMask(BORDER edge)
{
	switch (edge)
	{
		case BORDER::LEFT: return left_border;
		case BORDER::RIGHT: return right_border;
		case BORDER::TOP: return top_border; // co-ords switched
		case BORDER::BOTTOM: return bottom_border;
	}
}

void PosTracker::overlayPath(bool state)
{
	 path_overlay = state;
	 if ( ! pathFrame.empty() )
	 {
		pathFrame = cv::Scalar(0);
	 	debug_frame = cv::Scalar(0);
	 }
}

AudioProcessorEditor* PosTracker::createEditor()
{
	editor = new PosTrackerEditor(this, true);
	return editor;
}

void PosTracker::updateSettings()
{
	if ( editor != NULL )
		editor->updateSettings();
}

void PosTracker::setParameter(int paramIdx, float newVal)
{}


std::vector<std::string> PosTracker::getDeviceFormats()
{
	if ( ! currentCam->ready() )
		currentCam->open_device();
	currentCam->get_formats();
	return currentCam->get_format_descriptions();
}

void PosTracker::setDeviceFormat(std::string format)
{
	if ( ! currentCam->ready() )
		currentCam->open_device();
	std::vector<Formats*> formats = currentCam->get_formats();
	for ( auto & f : formats)
	{
		if ( f->get_description().compare(format) == 0 )
			currentCam->set_format(format);
	}
	camReady = true;
}

Formats * PosTracker::getCurrentFormat()
{
	return currentCam->get_current_format();
}

std::string PosTracker::getDeviceName()
{
	if ( currentCam )
		return currentCam->get_dev_name();
	else
		return "";
}

std::string PosTracker::getFormatName()
{
	return currentCam->get_format_name();
}

std::vector<std::string> PosTracker::getDeviceList()
{
	std::vector<std::string> devices = Camera::get_devices();
	return devices;
}

void PosTracker::createNewCamera(std::string dev_name)
{
	if ( currentCam != nullptr )
	{
		if ( currentCam->ready() )
		{
			currentCam->stop_device();
			currentCam->uninit_device();
			currentCam->close_device();
		}
		delete currentCam;
	}
	std::vector<std::string> devices = Camera::get_devices();
	for ( auto & dev : devices )
	{
		if ( dev.compare(dev_name) == 0 )
			currentCam = new Camera(dev_name);
	}
}

std::pair<int,int> PosTracker::getResolution()
{
	if ( currentCam )
	{
		if ( currentCam->get_current_format() )
		{
			auto format = currentCam->get_current_format();
			return std::make_pair<int,int>(format->width, format->height);
		}
	}
	else
		return std::make_pair<int,int>(1, 1);
}

void PosTracker::saveCustomParametersToXml(XmlElement* xml)
{
	xml->setAttribute("Type", "PosTracker");
	XmlElement * paramXml = xml->createNewChildElement("Parameters");
	paramXml->setAttribute("Brightness", getBrightness());
	paramXml->setAttribute("Contrast", getContrast());
	paramXml->setAttribute("Exposure", getExposure());
	paramXml->setAttribute("LeftBorder", left_border);
	paramXml->setAttribute("RightBorder", right_border);
	paramXml->setAttribute("TopBorder", top_border);
	paramXml->setAttribute("BottomBorder", bottom_border);
	paramXml->setAttribute("AutoExposure", auto_exposure);
	paramXml->setAttribute("OverlayPath", path_overlay);

	XmlElement * deviceXml = xml->createNewChildElement("Devices");
	deviceXml->setAttribute("Camera", getDeviceName());
	deviceXml->setAttribute("Format", getFormatName());
}

void PosTracker::loadCustomParametersFromXml()
{
	forEachXmlChildElementWithTagName(*parametersAsXml, paramXml, "Parameters")
	{
		if ( paramXml->hasAttribute("Brightness") )
			brightness = paramXml->getIntAttribute("Brightness");
		if ( paramXml->hasAttribute("Contrast") )
			contrast = paramXml->getIntAttribute("Contrast");
		if ( paramXml->hasAttribute("Exposure"))
			exposure = paramXml->getIntAttribute("Exposure");
		if ( paramXml->hasAttribute("LeftBorder") )
			left_border = paramXml->getIntAttribute("LeftBorder");
		if ( paramXml->hasAttribute("RightBorder") )
			right_border = paramXml->getIntAttribute("RightBorder");
		if ( paramXml->hasAttribute("TopBorder") )
			top_border = paramXml->getIntAttribute("TopBorder");
		if ( paramXml->hasAttribute("BottomBorder") )
			bottom_border = paramXml->getIntAttribute("BottomBorder");
		if ( paramXml->hasAttribute("AutoExposure") )
			auto_exposure = paramXml->getBoolAttribute("AutoExposure");
		if ( paramXml->hasAttribute("OverlayPath") )
			path_overlay = paramXml->getBoolAttribute("OverlayPath");
	}
	forEachXmlChildElementWithTagName(*parametersAsXml, deviceXml, "Devices")
	{
		if ( deviceXml->hasAttribute("Camera") )
			createNewCamera(deviceXml->getStringAttribute("Camera").toStdString());
		if ( deviceXml->hasAttribute("Format") )
		{
			if ( currentCam->ready() )
			{
				getDeviceList();
			}
			std::string fmt = deviceXml->getStringAttribute("Format").toStdString();
			setDeviceFormat(fmt);
		}
	}
	updateSettings();
}
