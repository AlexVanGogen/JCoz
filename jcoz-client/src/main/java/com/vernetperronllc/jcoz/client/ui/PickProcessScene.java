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
package com.vernetperronllc.jcoz.client.ui;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Scanner;
import java.util.Timer;
import java.util.TimerTask;

import com.sun.tools.attach.VirtualMachine;
import com.sun.tools.attach.VirtualMachineDescriptor;
import com.vernetperronllc.jcoz.JCozVMDescriptor;
import com.vernetperronllc.jcoz.client.cli.LocalProcessWrapper;
import com.vernetperronllc.jcoz.client.cli.RemoteServiceWrapper;
import com.vernetperronllc.jcoz.client.cli.TargetProcessInterface;
import com.vernetperronllc.jcoz.service.JCozException;
import com.vernetperronllc.jcoz.service.VirtualMachineConnectionException;

import javafx.beans.value.ChangeListener;
import javafx.beans.value.ObservableValue;
import javafx.collections.FXCollections;
import javafx.collections.ObservableList;
import javafx.event.ActionEvent;
import javafx.event.EventHandler;
import javafx.geometry.Insets;
import javafx.scene.Scene;
import javafx.scene.control.Button;
import javafx.scene.control.Label;
import javafx.scene.control.ListView;
import javafx.scene.control.RadioButton;
import javafx.scene.control.TextField;
import javafx.scene.control.TextFormatter;
import javafx.scene.control.Toggle;
import javafx.scene.control.ToggleGroup;
import javafx.scene.control.Tooltip;
import javafx.scene.input.MouseEvent;
import javafx.scene.layout.GridPane;
import javafx.scene.text.Font;
import javafx.scene.text.FontWeight;
import javafx.scene.text.Text;
import javafx.stage.Stage;
import javafx.util.converter.NumberStringConverter;

public class PickProcessScene {
    
	private static Map<String, VirtualMachineDescriptor> activeJCozVMs;
	
	private static PickProcessScene ppScene = null;
	
	private final GridPane grid = new GridPane();
	
	private final Scene scene;
	
	/* LOCAL / REMOTE */
	private final ToggleGroup localRemoteGroup = new ToggleGroup();
    private final RadioButton localRadio = new RadioButton("Local Process");
    private final RadioButton remoteRadio = new RadioButton("Remote Process");
	private final TextField remoteHostName = new TextField();
	private final TextField remotePort = new TextField();
	private RemoteServiceWrapper remoteService = null;
	
	/* PROCESS SPECIFIC */
	private final ListView<String> vmList = new ListView<>();
	private final TextField klass = new TextField();
	private final TextField scope = new TextField();
	private final TextField lineNumber = new TextField();

	/* BUTTON */
	private final Button profileProcessBtn = new Button();
	
	/** Disable constructor */
	private PickProcessScene(final Stage stage) {
		// Set layout of grid
		this.grid.setHgap(10);
        this.grid.setVgap(10);
        this.grid.setPadding(new Insets(25, 25, 25, 25));
        
        // TITLE
        final Text scenetitle = new Text("Profile a process");
        scenetitle.setFont(Font.font("Tahoma", FontWeight.NORMAL, 20));
        this.grid.add(scenetitle, 0, 0, 2, 1);
        int currRow = 1;
        
        // LOCAL OR REMOTE
        localRadio.setToggleGroup(this.localRemoteGroup);
        remoteRadio.setToggleGroup(this.localRemoteGroup);
        localRadio.setSelected(true);
        this.grid.add(localRadio, 0, currRow++);
        this.grid.add(remoteRadio, 0, currRow++);
        this.localRemoteGroup.selectedToggleProperty().addListener(new ChangeListener<Toggle>() {
			@Override
			public void changed(
					ObservableValue<? extends Toggle> observable,
					Toggle oldValue,
					Toggle newValue) {
				boolean localSelected = newValue.equals(localRadio);
		        remoteHostName.setDisable(localSelected);
		        remotePort.setDisable(localSelected);
			}
        });
        final Label hostNameLabel = new Label("Hostname:");
        this.grid.add(hostNameLabel, 0, currRow);
        this.grid.add(this.remoteHostName, 1, currRow);
        this.remoteHostName.setDisable(true);
        currRow++;
        final Label portLabel = new Label("Port:");
        this.grid.add(portLabel, 0, currRow);
        this.grid.add(this.remotePort, 1, currRow);
        this.remotePort.setDisable(true);
        currRow++;
        final Button connectToServiceButton = new Button("Connect to remote host");
        this.grid.add(connectToServiceButton, 0, currRow);
        currRow++;
        connectToServiceButton.setOnAction(new EventHandler<ActionEvent>() { 
            @Override
            public void handle(ActionEvent event) {
		        if (remoteRadio.isSelected()) {
		        	String host = remoteHostName.getText();
		        	if (host.length() == 0) return;
		        	boolean makeNewService = (remoteService == null) ||
		        			!remoteService.getHost().equals(host);

		        	if (makeNewService) {
		        		try {
							remoteService = new RemoteServiceWrapper(host);
							System.out.println("Connected to remote host");
						} catch (JCozException e) {
							System.err.println("Unable to create connection to remote host " + host);
							e.printStackTrace();
							System.exit(1);
						}
		        	}
		        }
            }
        });

        // VM LIST
        this.updateLocalVMList();
        this.vmList.setPrefWidth(100);
        this.vmList.setPrefHeight(70);
        Timer vmListUpdateTimer = new Timer("vmList update");
        TimerTask vmListUpdateTimerTask = new TimerTask() {
        	@Override
        	public void run() {
        		if (localRadio.isSelected()) {
        			updateLocalVMList();
        		} else {
        			try {
						updateRemoteVMList(remoteService);
					} catch (JCozException e) {
						System.err.println("Unable to update the remote VM list");
						e.printStackTrace();
					}
        		}
        	}
        };
        vmListUpdateTimer.schedule(vmListUpdateTimerTask, 0, 2000);
        
        Timer buttonEnableTimer = new Timer("buttonEnable");
        TimerTask buttonUpdateTask = new TimerTask() {
        	@Override
        	public void run() {
        		String selectedItem = vmList.getSelectionModel().getSelectedItem();
        		boolean hasProcess = (selectedItem != null) && (!selectedItem.equals(""));
        		boolean hasClass = (klass.getText() != null) && !klass.getText().equals("");
        		boolean hasScope = (scope.getText() != null) && !scope.getText().equals("");
        		boolean hasLineNumber = (lineNumber != null) && !lineNumber.equals("");
        		profileProcessBtn.setDisable(
        				!hasProcess ||
        				!hasLineNumber ||
        				!hasScope ||
        				!hasClass);
        	}
        };
        buttonEnableTimer.schedule(buttonUpdateTask, 0, 100);

        this.grid.add(this.vmList, 0, currRow, 5, 1);
        currRow++;
        
        // Scope text element.
        final Label packageLabel = new Label("Profiling scope (package):");
        this.grid.add(packageLabel, 0, currRow);
        this.grid.add(this.scope, 1, currRow);
        
        // Scope text element.
        final Label classLabel = new Label("Profiling class:");
        this.grid.add(classLabel, 3, currRow);
        this.grid.add(this.klass, 4, currRow);
        currRow++;

        // Progress point element.
        final Label lineNumberLabel = new Label("Line number:");
        this.grid.add(lineNumberLabel, 0, currRow);
        this.lineNumber.setTextFormatter(
        		new TextFormatter<>(new NumberStringConverter()));
        this.grid.add(this.lineNumber, 1, currRow);
        currRow++;

        this.profileProcessBtn.setText("Profile process");
        this.profileProcessBtn.setDisable(true);
        this.profileProcessBtn.setOnAction(new EventHandler<ActionEvent>() { 
            @Override
            public void handle(ActionEvent event) {
                String chosenProcess = vmList.getSelectionModel().getSelectedItem();
                try {
                    TargetProcessInterface profiledClient;
                    if (localRadio.isSelected()) {
                    	// Do local profiling
                    } else {
                    	int remotePid = getPidFromProcessString(chosenProcess);
                    	profiledClient = remoteService.attachToProcess(remotePid);
	                    setClientParameters(profiledClient);
	                    profiledClient.startProfiling();
	                    
	                    stage.setScene(VisualizeProfileScene.getVisualizeProfileScene(
	                    		profiledClient,stage));
                    }
                } catch (JCozException e) {
                    System.err.println("A JCoz exception was thrown.");
                    System.err.println(e);
                }
            }
        });
        this.grid.add(this.profileProcessBtn, 0, 10);
        
        this.scene = new Scene(this.grid, 980, 600);
	}
	
	/**
	 * Get the PID from a string of the form PID: <pid> - Name: <name>.
	 */
	private int getPidFromProcessString(String processString) {
		String pidStart = processString.substring(5, processString.length());
		Scanner pidScanner = new Scanner(pidStart);
		
		int pid = pidScanner.nextInt();
		pidScanner.close();
		
		return pid;
	}
	
	private void setClientParameters(TargetProcessInterface profiledClient) throws JCozException {
        String className = klass.getText();
        int lineNo = Integer.parseInt(lineNumber.getText());
        
        profiledClient.setProgressPoint(className, lineNo);
        profiledClient.setScope(this.scope.getText());
	}

	/**
	 * Update the VM list to display the current list of locally running VMs. 
	 */
	private void updateLocalVMList() {
		final Map<String, VirtualMachineDescriptor> vmDesciptors = 
				PickProcessScene.getLocalJCozVMList();

        List<String> vmNameList = new ArrayList<>(vmDesciptors.keySet());
        ObservableList<String> items = FXCollections.observableList(vmNameList);
        this.vmList.setItems(items);
	}
	
	/**
	 * Update the VM list from a remote service connection.
	 * @param service
	 * @throws JCozException
	 */
	private void updateRemoteVMList(RemoteServiceWrapper service) throws JCozException {
		// If we haven't made a service connection, empty the list and return.
		if (service == null) {
			this.vmList.setItems(null);
			return;
		}
		
		final List<JCozVMDescriptor> vmDescriptors = 
				service.listRemoteVirtualMachines();
		
		List<String> vmNameList = new ArrayList<>();
		// PID: <pid> - Name: <name>
		for (JCozVMDescriptor descriptor : vmDescriptors) {
			vmNameList.add("PID: " + descriptor.getPid() +
					" - Name: " + descriptor.getDisplayName());
		}
		
        ObservableList<String> items = FXCollections.observableList(vmNameList);
        this.vmList.setItems(items);
	}
	
	public Scene getScene() {
		return this.scene;
	}
	
	public VirtualMachineDescriptor getChosenVMDescriptor() {
		String chosenProcess = this.vmList.getSelectionModel().getSelectedItem();
		
		return PickProcessScene.activeJCozVMs.get(chosenProcess);
	}
	
	public void setScope(String scope) {
		this.scope.setText(scope);
	}
	
	public String getScope() {
		return this.scope.getText();
	}
	
	public void setProfiledClass(String klass) {
		this.klass.setText(klass);
	}
	
	public String getProfiledClass() {
		return this.klass.getText();
	}
	
	public static Scene getPickProcessScene(final Stage stage) {
		if (ppScene == null) {
			ppScene = new PickProcessScene(stage);
		}
		
		return PickProcessScene.ppScene.getScene();
	}
	
    /**
     * Search through the list of running VMs on the localhost
     * and attach to a JCoz Profiler instance.
     * @return Map<String, VirtualMachineDescriptor> A list of the VirtualMachines that
     *      should be queried for being profilable.
     */
    private static Map<String, VirtualMachineDescriptor> getLocalJCozVMList() {
        PickProcessScene.activeJCozVMs = new HashMap<>();
        List<VirtualMachineDescriptor> vmDescriptions = VirtualMachine.list();
        for(VirtualMachineDescriptor vmDesc : vmDescriptions){
        	activeJCozVMs.put(vmDesc.displayName(), vmDesc);
        }
        
        return activeJCozVMs;
    }
}

