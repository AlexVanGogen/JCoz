/*
 * NOTICE
 *
 * Copyright (c) 2016 David C Vernet and Matthew J Perron. All rights reserved.
 *
 * Unless otherwise noted, all of the material in this file is Copyright (c) 2016
 * by David C Vernet and Matthew J Perron. All rights reserved. No part of this file
 * may be reproduced, published, distributed, displayed, performed, copied,
 * stored, modified, transmitted or otherwise used or viewed by anyone other
 * than the authors (David C Vernet and Matthew J Perron),
 * for either public or private use.
 *
 * No part of this file may be modified, changed, exploited, or in any way
 * used for derivative works or offered for sale without the express
 * written permission of the authors.
 *
 * This file has been modified from lightweight-java-profiler
 * (https://github.com/dcapwell/lightweight-java-profiler). See APACHE_LICENSE for
 * a copy of the license that was included with that original work.
 */
package com.vernetperronllc.jcoz.client.cli;

import java.io.IOException;
import java.util.List;

import org.apache.commons.cli.CommandLine;
import org.apache.commons.cli.CommandLineParser;
import org.apache.commons.cli.DefaultParser;
import org.apache.commons.cli.Option;
import org.apache.commons.cli.Options;
import org.apache.commons.cli.OptionGroup;
import org.apache.commons.cli.ParseException;

import com.sun.tools.attach.VirtualMachine;
import com.sun.tools.attach.VirtualMachineDescriptor;
import com.vernetperronllc.jcoz.profile.Experiment;
import com.vernetperronllc.jcoz.service.JCozException;
import com.vernetperronllc.jcoz.service.VirtualMachineConnectionException;


/**
 * @author matt
 *
 */
public class JCozCLI {

	public static void main(String[] args) throws ParseException, VirtualMachineConnectionException, JCozException, IOException, InterruptedException{
		Options ops = new Options();
		
		Option ppClassOption = new Option("c", "ppClass", true, "Class of ProgressPoint");
		ppClassOption.setRequired(true);
		ops.addOption(ppClassOption);
		
		Option ppLineNoOption = new Option("l", "ppLineNo", true, "Line number of progress point");
		ppLineNoOption.setRequired(true);
		ops.addOption(ppLineNoOption);
		
		Option pidOption = new Option("p", "pid", true, "ProcessID to com.vernetperronllc.jcoz.profile");
		pidOption.setRequired(true);
		ops.addOption(pidOption);
		
		Option scopeOption = new Option("s", "scope", true, "scope to com.vernetperronllc.jcoz.profile (package)");
		scopeOption.setRequired(true);
		ops.addOption(scopeOption);
		
		CommandLineParser parser = new DefaultParser();
		CommandLine cl = parser.parse(ops, args);
		String ppClass = cl.getOptionValue('c');
		String scopePkg = cl.getOptionValue('s');
		int ppLineNo;
		int pid;
		try{
			ppLineNo = Integer.parseInt(cl.getOptionValue('l'));
		}catch(NumberFormatException e){
			System.err.println("Invalid Line Number : "+ cl.getOptionValue('l'));
			return;
		}
		try{
			pid = Integer.parseInt(cl.getOptionValue('p'));
		}catch(NumberFormatException e){
			System.err.println("Invalid pid : "+ cl.getOptionValue('l'));
			return;
		}
		VirtualMachineDescriptor descriptor = null;
		for(VirtualMachineDescriptor vmDesc : VirtualMachine.list()){
			if (vmDesc.id().equals(Integer.toString(pid))){
				descriptor = vmDesc;
				break;
			}
		}
		if(descriptor == null){
			System.err.println("Could not find java process with pid : "+ pid);
			return;
		}
		
		final LocalProcessWrapper wrapper = new LocalProcessWrapper(descriptor);
		//catch SIGINT and end profiling
		Runtime.getRuntime().addShutdownHook(new Thread()
        {
            @Override
            public void run()
            {
                try {
					wrapper.endProfiling();
					System.exit(0);
				} catch (JCozException e) {
					// we are dying, do nothing
				}
            }
        });
		wrapper.setProgressPoint(ppClass, ppLineNo);
		wrapper.setScope(scopePkg);
		wrapper.startProfiling();
		while(true){
			for(Experiment e : wrapper.getProfilerOutput()){
				System.out.println(e.toString());
			}
			Thread.sleep(1000);
		}
		
		// search through and 
		
		
	}
}
