Scriptname GardenOracleState

State Busy
	Function Run()
		Int local = 7
	EndFunction
EndState

Function Run()
	GotoState("Busy")
EndFunction
