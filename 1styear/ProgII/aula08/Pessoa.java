// Marta Oliveira nº 97613
//Bruno Silva nº 97931
//Francisco Cardita 97640
//Leonardo Fiuza nº 97772


package p2utils;
public class Pessoa implements Comparable<Pessoa> {
	private String name;
	private Data bDay;
	
	
	public String getName(){ return name;}
	
	public Data getbDay(){ return bDay;}
	
	
	public Pessoa(String name, Data bDay){
		this.name = name;
		this.bDay = bDay;
	}
	
	
	public String toString(){
		return (name + " " + bDay.dateToString());
	}
	
	
	public int compareTo(Pessoa p2){
		if(this.bDay.getMonth() > p2.bDay.getMonth()) //p1 faz depois de p2
			return(this.bDay.getMonth() - p2.bDay.getMonth()); // positivo
		else if(this.bDay.getMonth() < p2.bDay.getMonth()) //p1 faz antes de p2
			return(this.bDay.getMonth() - p2.bDay.getMonth()); // negativo
		else return(this.bDay.getDay() - p2.bDay.getDay()); //p1 faz no mesmo mes de p2
	}
}
