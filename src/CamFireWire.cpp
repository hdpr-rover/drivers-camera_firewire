/*
 * File:   CamFireWire.cpp
 * Author: Christopher Gaudig, DFKI Bremen
 *
 * Created on February 23, 2010, 4:57 PM
 */

#include <iostream>
#include <opencv/highgui.h>
#include "CamFireWire.h"

namespace camera
{

CamFireWire::CamFireWire()
{
    // create a new firewire bus device
    dc_device = dc1394_new();
    
    // init parameters
    dc_camera = NULL;
    hdr_enabled = false;
    multi_shot_count = 0;

    // find the cameras on the bus
    dc1394camera_list_t *list;
    dc1394_camera_enumerate (dc_device, &list);
    
    // use the first camera on the bus to issue a bus reset
    dc_camera = dc1394_camera_new(dc_device, list->ids[0].guid);
    dc1394_reset_bus(dc_camera);
    dc1394_camera_free(dc_camera);
}

CamFireWire::~CamFireWire()
{
}

// list all cameras on the firewire bus
int CamFireWire::listCameras(std::vector<CamInfo>&cam_infos)const
{

    dc1394camera_list_t *list;
    dc1394error_t err;

    // get list of available cameras
    err = dc1394_camera_enumerate (dc_device, &list);

    // temporary camera pointer (for getting the cam_infos)
    dc1394camera_t *tmp_camera;

    // if no camera is found on the bus
    if (list->num == 0)
        std::cout << "no cam found!" << std::endl;

    // get the cam_info for each camera on the bus
    for (int i = 0 ; i < list->num ; i++)
    {
	// get the i-th camera
        tmp_camera = dc1394_camera_new(dc_device, list->ids[i].guid);
        
	// get, set and display the corresponding cam_info
	CamInfo cam_info;
        cam_info.unique_id = tmp_camera->guid;
        cam_info.display_name = tmp_camera->model;
        cam_info.interface_type = InterfaceFirewire;
        showCamInfo(cam_info);

	// add the current cam_info to the vector cam_infos and release the temporary camera
        cam_infos.push_back(cam_info);
        dc1394_camera_free(tmp_camera);
    }

    // return the number of cameras found
    return list->num;
}

// open the camera specified by the CamInfo cam
bool CamFireWire::open(const CamInfo &cam,const AccessMode mode)
{
    dc1394camera_list_t *list;
    dc1394error_t err;

    // get list of available cameras
    err = dc1394_camera_enumerate (dc_device, &list);
    
    // get camera with the given uid and release the list
    dc_camera = dc1394_camera_new(dc_device, cam.unique_id);
    dc1394_camera_free_list(list);
    
    // set the current grab mode to "Stop"
    act_grab_mode_= Stop;
}

// returns true if the camera is open
bool CamFireWire::isOpen()const
{
    if (dc_camera == NULL)
        return false;
    else
        return true;
}

// stop capturing and get rid of the camera
bool CamFireWire::close()
{
    if (dc_camera != NULL)
    {
        dc1394_capture_stop(dc_camera);
        dc1394_camera_free(dc_camera);
        dc_camera = NULL;
    }
}

// start grabbing using the given GrabMode mode and write frame into a buffer of lenght buffer_len
bool CamFireWire::grab(const GrabMode mode, const int buffer_len)
{
    //check if someone tries to change the grab mode
    //during grabbing
    if (act_grab_mode_ != Stop && mode != Stop)
    {
        if (act_grab_mode_ != mode)
            throw std::runtime_error("Stop grabbing before switching the"
                                     " grab mode!");
        else
            return true;
    }

    // start grabbing using the given GrabMode mode
    switch (mode)
    {
    // stop transmitting and capturing frames
    case Stop:
        dc1394_video_set_transmission(dc_camera, DC1394_OFF);
        dc1394_capture_stop(dc_camera);
        break;
    
    // grab one frame only (one-shot mode)
    case SingleFrame:
        if (!dc_camera->one_shot_capable)
            throw std::runtime_error("Camera is not one-shot capable!");
        dc1394_capture_setup(dc_camera,1,DC1394_CAPTURE_FLAGS_DEFAULT);
	dc1394_video_set_one_shot(dc_camera, DC1394_ON);
        break;
	
    // grab N frames (N previously defined by setting AcquisitionFrameCount
    case MultiFrame:
        dc1394_capture_setup(dc_camera,buffer_len,DC1394_CAPTURE_FLAGS_DEFAULT);
	if(multi_shot_count == 0)
	  throw std::runtime_error("Set AcquisitionFrameCount (multi-shot) to a positive number before calling grab()!");
	dc1394_video_set_multi_shot(dc_camera, multi_shot_count, DC1394_ON);
	//start transmitting frames
	dc1394_video_set_transmission(dc_camera,DC1394_OFF);
        break;
	
    // start grabbing frames continuously (using the framerate set beforehand)
    case Continuously:
        dc1394_capture_setup(dc_camera,buffer_len,DC1394_CAPTURE_FLAGS_DEFAULT);
        dc1394_video_set_transmission(dc_camera,DC1394_ON);
        break;
    default:
        throw std::runtime_error("Unknown grab mode!");
    }
    act_grab_mode_ = mode;
}

// retrieve a frame from the camera
bool CamFireWire::retrieveFrame(Frame &frame,const int timeout)
{
    // dequeue a frame using the dc1394-frame tmp_frame
    dc1394video_frame_t *tmp_frame;
    dc1394_capture_dequeue(dc_camera, DC1394_CAPTURE_POLICY_WAIT, &tmp_frame );
    
    // create a new DFKI frame and copy the data from tmp_frame
    Frame tmp;
    tmp.init(image_size_.width, image_size_.height, data_depth, MODE_BAYER_RGGB, hdr_enabled);
    tmp.setImage((const char *)tmp_frame->image, tmp_frame->size[0] * tmp_frame->size[1]);
    
    // convert the bayer pattern image to RGB
	//camera::Helper::convertBayerToRGB24(tmp_frame->image, test.getImagePtr(), 640, 480, MODE_BAYER_RGGB);
    filter::Frame2RGGB::process(tmp,frame);
    
    // re-queue the frame previously used for dequeueing
    dc1394_capture_enqueue(dc_camera,tmp_frame);
}

// sets the frame size, mode, color depth and whether frames should be resized
bool CamFireWire::setFrameSettings(const frame_size_t size,
                                   const frame_mode_t mode,
                                   const  uint8_t color_depth,
                                   const bool resize_frames)
{
    dc1394video_modes_t vmst;
    dc1394_video_get_supported_modes(dc_camera,&vmst);

    switch (mode)
    {
    case MODE_BAYER_RGGB:
        std::cerr << "set mode " << dc1394_video_set_mode(dc_camera, DC1394_VIDEO_MODE_640x480_MONO8)<< std::endl;
	data_depth = 8;
        break;
    default:
        throw std::runtime_error("Unknown frame mode!");
    }
    image_size_ = size;
    image_mode_ = mode;
    image_color_depth_ = color_depth;
}

// (should) return true if the camera is ready for the next one-shot capture
bool CamFireWire::isReadyForOneShot()
{
    uint32_t one_shot;
    
    // get the camera's one-shot register
    dc1394_get_control_register(dc_camera,0x0061C,&one_shot);
    printf("one shot is %x ",one_shot);
    fflush(stdout);
    
    // the first bit is 1 when the cam is not ready and 0 when ready for a one-shot
    return (one_shot & 0x80000000UL) ? false : true;
}

// set integer-valued attributes
bool CamFireWire::setAttrib(const int_attrib::CamAttrib attrib,const int value)
{
    // the feature (attribute) we want to set
    dc1394feature_t feature;
    
    switch (attrib)
    {
    // set the shutter time
    case int_attrib::ExposureValue:
        feature = DC1394_FEATURE_SHUTTER;
        dc1394_feature_set_value(dc_camera, feature , value);
        break;
	
    // set the gain
    case int_attrib::GainValue:
        feature = DC1394_FEATURE_GAIN;
        dc1394_feature_set_value(dc_camera, feature , value);
        break;
	
    // set the red white-balance value
    case int_attrib::WhitebalValueRed:
        uint32_t ub;
        uint32_t vr;
        dc1394_feature_whitebalance_get_value(dc_camera, &ub, &vr);
        dc1394_feature_whitebalance_set_value(dc_camera,ub,value);
        break;
	
    // set the blue white-balance value
    case int_attrib::WhitebalValueBlue:
        dc1394_feature_whitebalance_get_value(dc_camera, &ub, &vr);
        dc1394_feature_whitebalance_set_value(dc_camera,value,vr);
        break;
	
    // set the camera's isochronous transfer speed on the bus in Mbps
    case int_attrib::IsoSpeed:
        dc1394speed_t speed;
        switch (value)
        {
        case 400:
            speed = DC1394_ISO_SPEED_400;
            break;
        case 200:
            speed = DC1394_ISO_SPEED_200;
            break;
        case 100:
            speed = DC1394_ISO_SPEED_100;
            break;
        default:
            throw std::runtime_error("Unsupported Iso Speed!");
        };
        dc1394_video_set_iso_speed(dc_camera, speed);
        break;
	
    // set the number of frames to capture in multi-shot mode
    case int_attrib::AcquisitionFrameCount:
        multi_shot_count = value;
        break;
	
    // the attribute given is not supported (yet)
    default:
        std::cerr << attrib;
        throw std::runtime_error("Unknown attribute!");
    };

    return false;
};

// set enum attributes
bool CamFireWire::setAttrib(const enum_attrib::CamAttrib attrib)
{
    // the feature (attribute) we want to set
    dc1394feature_t feature;
    
    // the (enum) value we want to set
    dc1394switch_t value;
    
    switch (attrib)
    {
    // turn gamma on
    case enum_attrib::GammaToOn:
        feature = DC1394_FEATURE_GAMMA;
        value = DC1394_ON;
        break;
	
    // turn gamma off
    case enum_attrib::GammaToOff:
        feature = DC1394_FEATURE_GAMMA;
        value = DC1394_OFF;
        break;
	
    // attribute unknown or not supported (yet)
    default:
        throw std::runtime_error("Unknown attribute!");
    };

    // set the desired attribute/feature value
    dc1394_feature_set_power(dc_camera, feature , value);

    return false;
};

// set double-valued attributes
bool CamFireWire::setAttrib(const double_attrib::CamAttrib attrib, const double value)
{
    // the feature/attribute we want to set
    dc1394feature_t feature;
    
    // the desired video framerate
    dc1394framerate_t framerate;
    
    switch (attrib)
    {
    // set the framerate
    case double_attrib::FrameRate:
        if (value==30)
            framerate = DC1394_FRAMERATE_30;
        else if (value==60)
            framerate = DC1394_FRAMERATE_60;
        else if (value==15)
            framerate = DC1394_FRAMERATE_15;
        else
            throw std::runtime_error("Framerate not supported! Use 15, 30 or 60 fps.");

	// the actual framerate-setting
        dc1394_video_set_framerate(dc_camera, framerate);
        break;
    
    // attribute unknown or not supported (yet)
    default:
        throw std::runtime_error("Unknown attribute!");
    };
    
    return false;
};

}
