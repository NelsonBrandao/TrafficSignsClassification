#include <jni.h>

#include <android/log.h>
#define APPNAME "TrafficZigns"

#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/objdetect/objdetect.hpp>
#include <opencv2\ml\ml.hpp>

extern "C"
{
	#define AREA_THRESHOLD      1000

	// Global variables _____
	cv::RandomTrees m_classifier;
	std::vector<cv::Mat> m_templates;

	// Dependencies
	enum ColorChannel { Red, Blue, };
	enum HLS { H = 0, L = 1, S = 2, };

	uchar lerp(uchar a, uchar b, double t) { return (uchar)(a*(1-t)+b*t); }
	uchar smoothStep(uchar a, uchar b, double t) { return (uchar)lerp(a,b,(3*pow(t,2.0))-(2*pow(t,3.0))); }
	uchar lutH(uchar v, uchar centerV, uchar interval, uchar sigma)
	{
	    uchar m_result = 0;
	    if ((interval)&&(sigma<=(interval/2.0)))
	    {
	        int m_minRef = -(interval/2.0),
	        		m_maxRef = (interval/2.0),
	        		m_shifted = v-centerV;
	        m_shifted = (m_shifted>=(256+m_minRef))?m_shifted-256:m_shifted;
	        m_shifted = (m_shifted<=(-256+m_maxRef))?m_shifted+256:m_shifted;
	        if ((m_shifted>=m_minRef)&&(m_shifted<=m_maxRef))
	        {
	            m_result = 255;
	            if (m_shifted<(m_minRef+sigma))
	                m_result = smoothStep(0,255,(m_shifted-m_minRef)/float(sigma));
	            if (m_shifted>(m_maxRef-sigma))
	                m_result = smoothStep(255,0,((m_shifted-m_maxRef)+sigma)/float(sigma));
	        }
	    }
	    return m_result;
	}
	uchar lutS(uchar v, uchar rampTo)
	{
		uchar m_result = 255;
		if (v<rampTo)
			m_result = smoothStep(0,rampTo,v/(double)rampTo);
		return m_result;
	}
	cv::Mat bgr_to_nhs(cv::Mat input, ColorChannel channel)
	{
	    cv::Mat m_result = cv::Mat::zeros(input.rows, input.cols, CV_8UC1);
	    cv::Mat ihls_image(input.rows, input.cols, CV_8UC3);
	    for (unsigned int j = 0; j < input.rows; j++)
	    {
	        const uchar* bgr_data = input.ptr<uchar> (j);
	        uchar* ihls_data = ihls_image.ptr<uchar> (j);
	        for (int k = 0; k < input.cols; k++)
	        {
	            unsigned int b = *bgr_data++;
	            unsigned int g = *bgr_data++;
	            unsigned int r = *bgr_data++;
	            // ______________________________________________________
	            // SATURATION _____
	            float saturation;
	            unsigned int max = b, min = b;
	            if (r > max) max = r;
	            if (r < min) min = r;
	            if (g > max) max = g;
	            if (g < min) min = g;
	            saturation = max - min;
	            // LUMINANCE _____
	            // L = 0.210R + 0.715G + 0.072B
	            float luminance = (0.210f * r) + (0.715f * g) + (0.072f * b);
	            // HUE _____
	            float hue = 0.0f;
	            // It calculates theta in radiands based on the equation provided in Valentine thesis.
	            float theta = acos((r - (g * 0.5) - (b * 0.5)) /
	              (float)sqrtf((r * r) + (g * g) + (b * b) - (r * g) - (r * b) - (g * b)));
	            if (b <= g) hue = theta;
	            else hue = (2 * M_PI) - theta;
	            // ______________________________________________________
	            *ihls_data++ = (uchar)saturation;
	            *ihls_data++ = (uchar)luminance;
	            *ihls_data++ = (uchar)(hue * 255 / (2 * M_PI));
	        }
	    }
	    if (channel == Red)
	    {
	        for (unsigned int j = 0; j < ihls_image.rows; j++)
	        {
	            const uchar *ihls_data = ihls_image.ptr<uchar> (j);
	            uchar *nhs_data = m_result.ptr<uchar> (j);
	            for (int k = 0; k < ihls_image.cols; k++)
	            {
	                uchar s = *ihls_data++;
	                uchar l = *ihls_data++;
	                uchar h = *ihls_data++;
	                *nhs_data++ = ((h < 15 || h > 240) && s > 25)?255:0;//((h < 163 && h > 134) && s > 39)?255:0;
	            }
	        }
	    }
	    if (channel == Blue)
	    {
	        for (unsigned int j = 0; j < ihls_image.rows; j++)
	        {
	            const uchar *ihls_data = ihls_image.ptr<uchar> (j);
	            uchar *nhs_data = m_result.ptr<uchar> (j);
	            for (int k = 0; k < ihls_image.cols; k++)
	            {
	                uchar s = *ihls_data++;
	                uchar l = *ihls_data++;
	                uchar h = *ihls_data++;
	                *nhs_data++ = ((h < 163 && h > 134) && s > 39)?255:0;
	            }
	        }
	    }
	    return m_result;
	}
	void GetJStringContent(JNIEnv *AEnv, jstring AStr, std::string &ARes)
	{
		if (!AStr) { ARes.clear(); return; }
		const char *s = AEnv->GetStringUTFChars(AStr,NULL);
		ARes=s;
		AEnv->ReleaseStringUTFChars(AStr,s);
	}

	// MATCHING ___________________________________________________________________________________________________________________________________________
    enum SignalType { Triangle, Circle, Square, };
    typedef struct { SignalType type; cv::Rect position; } Match;
    typedef struct { Match match; double discrepancy; } MatchCandidate;
    std::vector<Match> performMatch(cv::Mat image)
    {
        std::vector<Match> m_result;
        std::vector<std::vector<cv::Point> > m_currentContours;
        cv::findContours(image, m_currentContours, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE);
        for (unsigned int i = 0; i < m_currentContours.size(); i++)
        {
            if (m_currentContours.at(i).size() && (cv::contourArea(m_currentContours.at(i)) > AREA_THRESHOLD))
            {
                std::vector<MatchCandidate> m_resultCandidates;

                cv::Rect m_shapeBB = cv::boundingRect(m_currentContours.at(i));
                if ((m_shapeBB.width < m_shapeBB.height + (0.25 * m_shapeBB.height)) &&
                    (m_shapeBB.width > m_shapeBB.height - (0.25 * m_shapeBB.height)))
                {
                    // Prepare Templates
                    cv ::Mat
                    m_triangleTemplateResized,
                    m_circleTemplateResized,
                    m_squareTemplateResized;

                    cv::resize(m_templates.at(0),m_triangleTemplateResized,m_shapeBB.size(),0,0,CV_INTER_CUBIC);
                    cv::resize(m_templates.at(1),m_circleTemplateResized,m_shapeBB.size(),0,0,CV_INTER_CUBIC);
                    cv::resize(m_templates.at(2),m_squareTemplateResized,m_shapeBB.size(),0,0,CV_INTER_CUBIC);

                    // Look for triangles
                    std::vector<std::vector<cv::Point> > m_triangleContour, m_largestTriangleContour;
                    cv::findContours(m_triangleTemplateResized,m_triangleContour, CV_RETR_TREE, CV_CHAIN_APPROX_SIMPLE);

                    if (m_triangleContour.size())
                    {
                        m_largestTriangleContour.push_back(m_triangleContour.at(0));
                        for (unsigned int c = 0; c < m_triangleContour.size(); c++)
                            if (cv::contourArea(m_triangleContour.at(c)) > cv::contourArea(m_largestTriangleContour.at(0)))
                                m_largestTriangleContour.at(0) = m_triangleContour.at(c);

                        double val = cv::matchShapes(m_largestTriangleContour.at(0),m_currentContours.at(i),CV_CONTOURS_MATCH_I1,0);
                        if (val < 0.05)
                        {
                            Match match;
                            match.type = Triangle;
                            match.position = m_shapeBB;

                            MatchCandidate matchCandidate;
                            matchCandidate.match = match;
                            matchCandidate.discrepancy = val;

                            m_resultCandidates.push_back(matchCandidate);
                        }
                    }

                    // Look for circles
                    std::vector<std::vector<cv::Point> > m_circleContour, m_largestCircleContour;
                    cv::findContours(m_circleTemplateResized,m_circleContour, CV_RETR_TREE, CV_CHAIN_APPROX_SIMPLE);
                    if (m_circleContour.size())
                    {
                        m_largestCircleContour.push_back(m_circleContour.at(0));
                        for (unsigned int c = 0; c < m_circleContour.size(); c++)
                            if (cv::contourArea(m_circleContour.at(c)) > cv::contourArea(m_largestCircleContour.at(0)))
                                m_largestCircleContour.at(0) = m_circleContour.at(c);

                        double val = cv::matchShapes(m_largestCircleContour.at(0),m_currentContours.at(i),CV_CONTOURS_MATCH_I1,0);
                        if (val < 0.05)
                        {
                            Match match;
                            match.type = Circle;
                            match.position = m_shapeBB;

                            MatchCandidate matchCandidate;
                            matchCandidate.match = match;
                            matchCandidate.discrepancy = val;

                            m_resultCandidates.push_back(matchCandidate);
                        }
                    }

                    // Look for squares
                    std::vector<std::vector<cv::Point> > m_squareContour, m_largestSquareContour;
                    cv::findContours(m_squareTemplateResized,m_squareContour, CV_RETR_TREE, CV_CHAIN_APPROX_SIMPLE);
                    if (m_squareContour.size())
                    {
                        m_largestSquareContour.push_back(m_squareContour.at(0));
                        for (unsigned int c = 0; c < m_squareContour.size(); c++)
                            if (cv::contourArea(m_squareContour.at(c)) > cv::contourArea(m_largestSquareContour.at(0)))
                                m_largestSquareContour.at(0) = m_squareContour.at(c);

                        double val = cv::matchShapes(m_largestSquareContour.at(0),m_currentContours.at(i),CV_CONTOURS_MATCH_I1,0);
                        if (val < 0.05)
                        {
                            Match match;
                            match.type = Square;
                            match.position = m_shapeBB;

                            MatchCandidate matchCandidate;
                            matchCandidate.match = match;
                            matchCandidate.discrepancy = val;

                            m_resultCandidates.push_back(matchCandidate);
                        }
                    }

                    if (m_resultCandidates.size())
                    {
                        int bestCandidateIndex = 0;
                        double lowestDiscrepancy = DBL_MAX;
                        for (int j = 0; j < m_resultCandidates.size(); j++)
                            if (m_resultCandidates.at(j).discrepancy < lowestDiscrepancy)
                            {
                                lowestDiscrepancy = m_resultCandidates.at(j).discrepancy;
                                bestCandidateIndex = j;
                            }
                        m_result.push_back(m_resultCandidates.at(bestCandidateIndex).match);
                    }
                }
            }
        }
        return m_result;
    }
    // ________________________________________________________________________________________________________________________________________________________

    // CLASSIFICATION ______________________________________________________________________________________________________________________________________
    float performPrediction(std::vector<float> instance)
    {
    	cv::Mat m_data = cv::Mat::zeros(1, instance.size(), CV_32FC1);
    	for (int i = 0; i < instance.size(); i++)
    		m_data.at<float>(i) = instance.at(i);
        return m_classifier.predict(m_data);
    }
    // ________________________________________________________________________________________________________________________________________________________

	JNIEXPORT jboolean JNICALL Java_mei_ta_trafficzigns_MainActivity_initTrafficZignsDetector(JNIEnv *env, jobject obj, jlong triangle, jlong circle, jlong square, jstring modelPath);
	JNIEXPORT jboolean JNICALL Java_mei_ta_trafficzigns_MainActivity_initTrafficZignsDetector(JNIEnv *env, jobject obj, jlong triangle, jlong circle, jlong square, jstring modelPath)
	{
		jboolean m_result = JNI_FALSE;

		// Load templates for matching
		m_templates.clear();
		cv::Mat m_triangle, m_circle, m_square;
		cv::cvtColor(*(cv::Mat*)triangle, m_triangle, CV_BGRA2GRAY);
		cv::cvtColor(*(cv::Mat*)circle, m_circle, CV_BGRA2GRAY);
		cv::cvtColor(*(cv::Mat*)square, m_square, CV_BGRA2GRAY);
		m_templates.push_back(m_triangle);
		m_templates.push_back(m_circle);
		m_templates.push_back(m_square);

		// Load classifier model
	    std::string m_modelPath;
	    GetJStringContent(env, modelPath, m_modelPath);
	    m_classifier.load(m_modelPath.c_str());

	    if ((m_templates.size() == 3) && m_modelPath.size())
	    	m_result = JNI_TRUE;

		return m_result;
	}

	JNIEXPORT jobjectArray JNICALL Java_mei_ta_trafficzigns_MainActivity_locateTrafficZigns(JNIEnv *env, jobject obj, jlong image);
	JNIEXPORT jobjectArray JNICALL Java_mei_ta_trafficzigns_MainActivity_locateTrafficZigns(JNIEnv *env, jobject obj, jlong image)
	{
__android_log_print(ANDROID_LOG_DEBUG, APPNAME, "LOG_1");
		cv::Mat m_bgrImage;
		cv::cvtColor(*(cv::Mat*)image, m_bgrImage, CV_RGBA2BGR);
__android_log_print(ANDROID_LOG_DEBUG, APPNAME, "LOG_2");
        // Color segmentation ___________________________________________________________________________________________________
		// - Initialize result buffer _
        cv::Mat m_colorChannels[3];
        for (unsigned int j = 0; j < 3; j++)
            m_colorChannels[j] = cv::Mat::zeros(m_bgrImage.rows,m_bgrImage.cols,CV_8UC1);

        // For red color segmentation - HLS colorspace _
        cv::Mat m_hlsImage,m_hlsImageChannels[3];
        cv::cvtColor(m_bgrImage,m_hlsImage,CV_BGR2HLS_FULL);
        cv::split(m_hlsImage,m_hlsImageChannels);
        for (int i = 0; i < (m_hlsImage.cols*m_hlsImage.rows); i++)
            m_colorChannels[0].data[i] = ((lutH(m_hlsImageChannels[H].data[i],0,25,0) & lutS(m_hlsImageChannels[S].data[i],255)) > 30)?255:0;
        // For red color segmentation - IHLS - NHS colorspace
        m_colorChannels[1] = bgr_to_nhs(m_bgrImage,Red);
        // For blue color segmentation - IHLS - NHS colorspace
        m_colorChannels[2] = bgr_to_nhs(m_bgrImage,Blue);
        // _______________________________________________________________________________________________________________________
__android_log_print(ANDROID_LOG_DEBUG, APPNAME, "LOG_3");
        // Noise Reduction _______________________________________________________________________________________________________
        int k_size = 11;
        cv::medianBlur(m_colorChannels[0],m_colorChannels[0],5);
        cv::dilate(m_colorChannels[0],m_colorChannels[0],getStructuringElement(cv::MORPH_RECT, cv::Size(k_size,k_size)));
        cv::erode(m_colorChannels[0],m_colorChannels[0],getStructuringElement(cv::MORPH_RECT, cv::Size(k_size,k_size)));
        cv::medianBlur(m_colorChannels[0],m_colorChannels[0],5);

        cv::medianBlur(m_colorChannels[1],m_colorChannels[1],5);
        cv::dilate(m_colorChannels[1],m_colorChannels[1],getStructuringElement(cv::MORPH_RECT, cv::Size(k_size,k_size)));
        cv::erode(m_colorChannels[1],m_colorChannels[1],getStructuringElement(cv::MORPH_RECT, cv::Size(k_size,k_size)));
        cv::medianBlur(m_colorChannels[1],m_colorChannels[1],5);

        cv::medianBlur(m_colorChannels[2],m_colorChannels[2],5);
        cv::dilate(m_colorChannels[2],m_colorChannels[2],getStructuringElement(cv::MORPH_RECT, cv::Size(k_size,k_size)));
        cv::erode(m_colorChannels[2],m_colorChannels[2],getStructuringElement(cv::MORPH_RECT, cv::Size(k_size,k_size)));
        // _______________________________________________________________________________________________________________________
__android_log_print(ANDROID_LOG_DEBUG, APPNAME, "LOG_4");
        std::vector<std::vector<cv::Point> > m_red1Contours, m_red2Contours, m_blueContours;
        cv::findContours(m_colorChannels[0], m_red1Contours, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE);
        cv::findContours(m_colorChannels[1], m_red2Contours, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE);
        cv::findContours(m_colorChannels[2], m_blueContours, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE);
        m_colorChannels[0].setTo(cv::Scalar(0,0,0));
        m_colorChannels[1].setTo(cv::Scalar(0,0,0));
        m_colorChannels[2].setTo(cv::Scalar(0,0,0));
        for (unsigned int j = 0; j < m_red1Contours.size(); j++)
            if (cv::contourArea(m_red1Contours.at(j)) > (((m_bgrImage.cols/(float)640)*5)*((m_bgrImage.rows/(float)480)*5)))
                cv::drawContours(m_colorChannels[0], m_red1Contours, j, cv::Scalar(255,255,255), CV_FILLED);
        for (unsigned int j = 0; j < m_red2Contours.size(); j++)
            if (cv::contourArea(m_red2Contours.at(j)) > (((m_bgrImage.cols/(float)640)*5)*((m_bgrImage.rows/(float)480)*5)))
                cv::drawContours(m_colorChannels[1], m_red2Contours, j, cv::Scalar(255,255,255), CV_FILLED);
        for (unsigned int j = 0; j < m_blueContours.size(); j++)
            if (cv::contourArea(m_blueContours.at(j)) > (((m_bgrImage.cols/(float)640)*5)*((m_bgrImage.rows/(float)480)*5)))
                cv::drawContours(m_colorChannels[2], m_blueContours, j, cv::Scalar(255,255,255), CV_FILLED);
__android_log_print(ANDROID_LOG_DEBUG, APPNAME, "LOG_5");
        // Match _
        std::vector<Match> m_red1Matches = performMatch(m_colorChannels[0]),
        		m_red2Matches = performMatch(m_colorChannels[1]),
        		m_blueMatches = performMatch(m_colorChannels[2]),
        		m_totalMatches;

        std::vector<cv::Rect> detectedObjects;
        for (int j = 0; j < m_red1Matches.size(); j++)
        {
        	bool m_exists = false;
        	if (detectedObjects.size())
				for (int r = 0; r < detectedObjects.size(); r++)
				{
					cv::Rect intersection = detectedObjects.at(r) & m_red1Matches.at(j).position;
					if (intersection.area())
						m_exists = true;
				}
        	if (!m_exists)
        	{
				m_totalMatches.push_back(m_red1Matches.at(j));
				detectedObjects.push_back(m_red1Matches.at(j).position);
        	}
        }
        for (int j = 0; j < m_red2Matches.size(); j++)
        {
        	bool m_exists = false;
        	if (detectedObjects.size())
				for (int r = 0; r < detectedObjects.size(); r++)
				{
					cv::Rect intersection = detectedObjects.at(r) & m_red2Matches.at(j).position;
					if (intersection.area())
						m_exists = true;
				}
        	if (!m_exists)
        	{
				m_totalMatches.push_back(m_red2Matches.at(j));
				detectedObjects.push_back(m_red2Matches.at(j).position);
        	}
        }
        for (int j = 0; j < m_blueMatches.size(); j++)
        {
        	bool m_exists = false;
        	if (detectedObjects.size())
				for (int r = 0; r < detectedObjects.size(); r++)
				{
					cv::Rect intersection = detectedObjects.at(r) & m_blueMatches.at(j).position;
					if (intersection.area())
						m_exists = true;
				}
        	if (!m_exists)
        	{
				m_totalMatches.push_back(m_blueMatches.at(j));
				detectedObjects.push_back(m_blueMatches.at(j).position);
        	}
        }
        // _
__android_log_print(ANDROID_LOG_DEBUG, APPNAME, "LOG_6");
        // Classification _
        std::vector<std::string> m_resultData;
        cv::Mat m_greyImage;
        cv::cvtColor(m_bgrImage, m_greyImage, CV_BGR2GRAY);
        for (unsigned int j = 0; j < m_totalMatches.size(); j++)
		{
        	cv::Mat m_hypothesis;
        	cv::resize(m_greyImage(m_totalMatches.at(j).position),m_hypothesis, cv::Size(40,40),0,0,CV_INTER_CUBIC);
        	cv::HOGDescriptor hog(cv::Size(40,40), cv::Size(10,10), cv::Size(5,5), cv::Size(5,5), 8, 1, -1, cv::HOGDescriptor::L2Hys, 0.2, false, cv::HOGDescriptor::DEFAULT_NLEVELS);

        	std::vector<float> descriptors;
        	std::vector<cv::Point>locations;
        	hog.compute(m_hypothesis,descriptors,cv::Size(0,0),cv::Size(0,0),locations);
        	descriptors.push_back(0.0);

			float m_classification = performPrediction(descriptors);

			std::string m_resStr;
			// Each frame is "C@(Xul,Yul)-(Xlr,Ylr)"
            char m_classBuffer[5], m_tlXBuffer[10], m_tlYBuffer[10], m_brXBuffer[10], m_brYBuffer[10];
            sprintf(m_classBuffer,"%d",(int)m_classification);
            sprintf(m_tlXBuffer,"%d",(int)m_totalMatches.at(j).position.tl().x);
            sprintf(m_tlYBuffer,"%d",(int)m_totalMatches.at(j).position.tl().y);
            sprintf(m_brXBuffer,"%d",(int)m_totalMatches.at(j).position.br().x);
            sprintf(m_brYBuffer,"%d",(int)m_totalMatches.at(j).position.br().y);

            m_resStr.append(m_classBuffer);
            m_resStr.append("@(");m_resStr.append(m_tlXBuffer);m_resStr.append(",");m_resStr.append(m_tlYBuffer);
            m_resStr.append(")-(");m_resStr.append(m_brXBuffer);m_resStr.append(",");m_resStr.append(m_brYBuffer);m_resStr.append(")");

			m_resultData.push_back(m_resStr);
		}
        // _
__android_log_print(ANDROID_LOG_DEBUG, APPNAME, "LOG_7");
		jobjectArray m_result = (jobjectArray)env->NewObjectArray(m_resultData.size(), env->FindClass("java/lang/String"), env->NewStringUTF(""));
		for (unsigned int i = 0; i < m_resultData.size(); i++)
			env->SetObjectArrayElement(m_result,i,env->NewStringUTF(m_resultData[i].c_str()));

		return m_result;
	}
}
