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

package com.vernetperronllc.jcoz.agent;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.ObjectOutputStream;
import java.lang.management.ManagementFactory;
import java.util.ArrayList;
import java.util.List;

import javax.management.InstanceAlreadyExistsException;
import javax.management.MBeanRegistrationException;
import javax.management.MBeanServer;
import javax.management.MalformedObjectNameException;
import javax.management.NotCompliantMBeanException;
import javax.management.ObjectName;

import com.vernetperronllc.jcoz.Experiment;

/**
 * Implementation of the mbean, controls the underlying native profiler
 * 
 * @author matt
 *
 */
public class JCozProfiler implements JCozProfilerMBean {

	/**
	 * is the mbean registered with the platform mbean server
	 */
	private static boolean registered_ = false;
	/**
	 * class of progress point
	 */
	private String progressPointClass_ = null;
	/**
	 * line number of progress point
	 */
	private Integer progressPointLineNo_ = null;
	/**
	 * scope to profile
	 */
	private String currentScope_ = null;
	/**
	 * is an experiment running
	 */
	private boolean experimentRunning_ = false;

	/*
	 * error return codes
	 */
	public static final int NORMAL_RETURN = 0;
	public static final int NO_PROGRESS_POINT_SET = 1;
	public static final int NO_SCOPE_SET = 2;
	public static final int CANNOT_CALL_WHEN_RUNNING = 3;
	public static final int PROFILER_NOT_RUNNING = 4;

	/**
	 * list of experiments run since last collected
	 */
	private List<Experiment> cachedOutput = new ArrayList<>();

	/**
	 * start profiling with the current scope and progress point
	 */
	public synchronized int startProfiling() {
		if (experimentRunning_) {
			return CANNOT_CALL_WHEN_RUNNING;
		}
		if (progressPointClass_ == null || progressPointLineNo_ == null) {
			return NO_PROGRESS_POINT_SET;
		}
		if (currentScope_ == null) {
			return NO_SCOPE_SET;
		}
		return startProfilingNative();
	}

	private native int startProfilingNative();

	/**
	 * end the current profiling
	 */
	public synchronized int endProfiling() {
		if (!experimentRunning_) {
			return PROFILER_NOT_RUNNING;
		}
		int returnCode = endProfilingNative();
		experimentRunning_ = false;
		return returnCode;
	}

	private native int endProfilingNative();

	/**
	 * set progress point
	 */
	public synchronized int setProgressPoint(String className, int lineNo) {
		if (experimentRunning_) {
			return CANNOT_CALL_WHEN_RUNNING;
		}
		return setProgressPointNative(className, lineNo);
	}

	private native int setProgressPointNative(String className, int lineNo);

	/**
	 * get the serialized output from recently run experiments
	 */
	public synchronized byte[] getProfilerOutput() throws IOException {
		if (!experimentRunning_) {
			return null;
		}
		System.out.println("get Profiler output, numExperiments "
				+ cachedOutput.size());
		ByteArrayOutputStream baos = new ByteArrayOutputStream();
		ObjectOutputStream oos = new ObjectOutputStream(baos);
		oos.writeInt(cachedOutput.size());
		for (Experiment e : cachedOutput) {
			e.serialize(oos);
		}
		System.out.println();
		clearCachedOutput();
		oos.flush();
		return baos.toByteArray();
	}

	/**
	 * clear the experiments in the buffer
	 */
	private synchronized void clearCachedOutput() {
		cachedOutput.clear();
	}

	/**
	 * method for the profiler to add an experiment to the buffer
	 * @param classSig
	 * @param lineNo
	 * @param speedup
	 * @param duration
	 * @param pointsHit
	 */
	private synchronized void cacheOutput(String classSig, int lineNo,
			float speedup, long duration, long pointsHit) {
		cachedOutput.add(new Experiment(classSig, lineNo, speedup, duration,
				pointsHit));
	}

	/**
	 * get the current experiment scope
	 */
	public synchronized String getCurrentScope() {
		return currentScope_;
	}

	/**
	 * set the scope to profile
	 */
	public synchronized int setScope(String scopePackage) {
		if (experimentRunning_) {
			return CANNOT_CALL_WHEN_RUNNING;
		}
		int scopeReturn = setScopeNative(scopePackage);
		if (scopeReturn == 0) {
			currentScope_ = scopePackage;
		}
		return scopeReturn;
	}

	private native int setScopeNative(String scopePackage);

	/**
	 * get the current progress point as a string class and line number are separated by a ':'
	 */
	public synchronized String getProgressPoint() {
		return progressPointClass_ + ":" + progressPointLineNo_;
	}

	/**
	 * register the profiler with the Platform mbean server
	 */
	public synchronized static void registerProfilerWithMBeanServer() {
		if (!registered_) {
			MBeanServer mbs = ManagementFactory.getPlatformMBeanServer();
			registered_ = true;
			try {
				JCozProfiler mbean = new JCozProfiler();
				synchronized (mbean) {
					mbs.registerMBean(mbean, getMBeanName());
				}
			} catch (InstanceAlreadyExistsException e) {
				// TODO Auto-generated catch block
				e.printStackTrace();
			} catch (MBeanRegistrationException e) {
				// TODO Auto-generated catch block
				e.printStackTrace();
			} catch (NotCompliantMBeanException e) {
				// TODO Auto-generated catch block
				e.printStackTrace();
			}
		}
	}

	/**
	 * get the ObjectName of this mbean
	 * @return
	 */
	public static ObjectName getMBeanName() {
		try {
			return new ObjectName(JCozProfiler.class.getPackage().getName()
					+ ":type=" + JCozProfiler.class.getSimpleName());
		} catch (MalformedObjectNameException e) {
			// do nothing, this should never be malformed
			throw new Error(e);
		}

	}
}
