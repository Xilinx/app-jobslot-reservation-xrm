## Copyright (C) 2021, Xilinx Inc - All rights reserved
# Xilinx U30 jobslot-reservation-xrm (jobslot-reservation-xrm)
# 
#  Licensed under the Apache License, Version 2.0 (the "License"). You may
#  not use this file except in compliance with the License. A copy of the
#  License is located at
# 
#      http://www.apache.org/licenses/LICENSE-2.0
# 
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#  License for the specific language governing permissions and limitations
#  under the License.

DEVDIR := Debug
DEBDIR := DEB_Release
RPMDIR := RPM_Release

dev: | $(DEVDIR)
	cd $(DEVDIR); \
	cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=$(CURDIR)/Debug ..; \
	make install

DEB: | $(DEBDIR)
	cd $(DEBDIR); \
	cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/xilinx/jobSlotReservation ..; \
	cpack -G DEB

RPM: | $(RPMDIR)
	cd $(RPMDIR); \
	cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/xilinx/jobSlotReservation ..; \
	cpack -G RPM

$(DEVDIR):
	mkdir -p $(DEVDIR);
$(DEBDIR):
	mkdir -p $(DEBDIR);
$(RPMDIR):
	mkdir -p $(RPMDIR);

clean:
	rm -rf $(DEVDIR) $(DEBDIR) $(RPMDIR);
