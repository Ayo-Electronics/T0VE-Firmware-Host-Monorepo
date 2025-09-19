
/*
 * app_msc_if.cpp
 *
 *  Created on: Sep 19, 2025
 *      Author: govis
 */

#include "app_msc_if.hpp"

//================= PUBLIC FUNCTIONS =============

MSC_Interface::MSC_Interface(MSC_Interface_Channel_t& channel):
msc_channel(channel)
{}

MSC_Interface::~MSC_Interface() {
    //go through the attached files; if they're not nullptr, detach them
    for (auto& file : msc_files) {
        if (file) detach_file(*file);
    }
}

void MSC_Interface::init() {
    //call the upstream init function, but that's it
	msc_channel.usb.init();
}

void MSC_Interface::connect_request() {
    //call the upstream connect_request function, but that's it
	msc_channel.usb.connect_request();
}

void MSC_Interface::disconnect_request() {
    //call the upstream disconnect_request function, but that's it
	msc_channel.usb.disconnect_request();
}

void MSC_Interface::attach_file(MSC_File& _file) {
    //find the first free spot in our list
    for (auto& file : msc_files) {
        if (!file) {
            file = &_file;
            //and save information about us to our file
            //so that it can destruct correctly, i.e. remove itself from the list
            _file.msc_if = this;
            return;
        }
    }
}

void MSC_Interface::detach_file(MSC_File& _file) {
    //find the file in our list and remove it
    //reset the metadata information about us in the file too
    for (auto& file : msc_files) {
        if (file == &_file) {
            file = nullptr;
            _file.msc_if = nullptr;
            return;
        }
    }
}

//================= PRIVATE FUNCTIONS =============
